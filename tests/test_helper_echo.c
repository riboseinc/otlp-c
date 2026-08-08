/* SPDX-License-Identifier: Apache-2.0 */
/*
 * In-process HTTP echo server implementation. POSIX only (test
 * code; the library is portable, the test helper is not).
 */
/* _DEFAULT_SOURCE for CLOCK_MONOTONIC + INADDR_LOOPBACK under glibc
 * with -std=c11. _DEFAULT_SOURCE implies _POSIX_C_SOURCE on glibc;
 * declaring _POSIX_C_SOURCE explicitly here hides INADDR_LOOPBACK
 * on macOS, so we keep just _DEFAULT_SOURCE. */
#define _DEFAULT_SOURCE

#include "test_helper_echo.h"

#if defined(_WIN32)
#error "test_helper_echo.c is POSIX-only; the Windows CI job runs unit tests via WSL."
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

struct echo_thread_arg
{
	struct echo_server *server;
};

/* ── Start / join ─────────────────────────────────────────────── */

static void *
echo_thread_main(void *arg);

otlp_status_t
echo_server_start(struct echo_server *s,
	echo_handler_t handler,
	size_t requests_to_serve)
{
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);
	pthread_t tid;
	struct echo_thread_arg *a;

	memset(s, 0, sizeof(*s));
	s->sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (s->sock_fd < 0)
		return OTLP_ERR_NETWORK;

	int yes = 1;
	(void) setsockopt(s->sock_fd,
		SOL_SOCKET,
		SO_REUSEADDR,
		(const char *) &yes,
		sizeof(yes));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0; /* kernel-chosen */
	if (bind(s->sock_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0)
		goto fail;
	if (listen(s->sock_fd, 8) < 0)
		goto fail;
	if (getsockname(s->sock_fd, (struct sockaddr *) &addr, &alen) < 0)
		goto fail;
	s->port = ntohs(addr.sin_port);

	s->handler = handler;
	s->requests_to_serve = requests_to_serve;
	otlp_atomic_store_u64(&s->requests_served, 0, OTLP_MEMORY_ORDER_RELAXED);
	otlp_atomic_store_u64(&s->requests_seen, 0, OTLP_MEMORY_ORDER_RELAXED);

	a = malloc(sizeof(*a));
	if (!a)
		goto fail;
	a->server = s;
	if (pthread_create(&tid, NULL, echo_thread_main, a) != 0)
	{
		free(a);
		goto fail;
	}
	pthread_detach(tid);
	/* RELEASE so all the setup above is visible to whichever thread
	 * observes running == 1 via an ACQUIRE load. */
	otlp_atomic_store_int(&s->running, 1, OTLP_MEMORY_ORDER_RELEASE);
	return OTLP_OK;

fail:
	if (s->sock_fd >= 0)
		close(s->sock_fd);
	s->sock_fd = -1;
	return OTLP_ERR_NETWORK;
}

otlp_status_t
echo_server_join(struct echo_server *s, uint64_t timeout_us)
{
	uint64_t deadline;
	uint64_t now;
	struct timespec ts;
	struct timespec mono;

	clock_gettime(CLOCK_MONOTONIC, &mono);
	now = (uint64_t) mono.tv_sec * 1000000ULL +
		(uint64_t) mono.tv_nsec / 1000ULL;
	deadline = now + timeout_us;

	/* ACQUIRE load pairs with the worker's RELEASE store of 0 on exit.
	 * Once we observe running == 0, every write the worker made before
	 * exiting (including the final requests_served value) is visible. */
	while (otlp_atomic_load_int(&s->running, OTLP_MEMORY_ORDER_ACQUIRE) &&
	       now < deadline)
	{
		ts.tv_sec = 0;
		ts.tv_nsec = 1000 * 100; /* 100us */
		nanosleep(&ts, NULL);
		clock_gettime(CLOCK_MONOTONIC, &mono);
		now = (uint64_t) mono.tv_sec * 1000000ULL +
			(uint64_t) mono.tv_nsec / 1000ULL;
	}
	return otlp_atomic_load_int(&s->running, OTLP_MEMORY_ORDER_ACQUIRE)
		? OTLP_ERR_TIMEOUT : OTLP_OK;
}

void
echo_server_stop(struct echo_server *s)
{
	if (!s || s->sock_fd < 0)
		return;
	/* Closing the listen fd makes the worker thread's accept() fail
	 * with EBADF/EINVAL; it then exits its loop. The thread is
	 * detached, so no join needed. */
	shutdown(s->sock_fd, SHUT_RDWR);
	close(s->sock_fd);
	s->sock_fd = -1;
	/* _stop does not flip running; the worker does that on its way out.
	 * Callers waiting via _join will observe the worker's release store. */
}

/* ── Internal: parse + serve one request ──────────────────────── */

static int
find_substring(const uint8_t *hay, size_t hay_len, const char *needle)
{
	size_t needle_len = strlen(needle);
	size_t i;

	if (hay_len < needle_len)
		return -1;
	for (i = 0; i <= hay_len - needle_len; i++)
		if (memcmp(hay + i, needle, needle_len) == 0)
			return (int) i;
	return -1;
}

static size_t
parse_content_length(const uint8_t *req, size_t hdr_end)
{
	const char *cl;
	size_t body_len = 0;

	for (cl = (const char *) req; cl < (const char *) req + hdr_end - 2;
		cl++)
	{
		if (strncasecmp(cl, "Content-Length:", 15) == 0)
		{
			cl += 15;
			while (*cl == ' ')
				cl++;
			while (*cl >= '0' && *cl <= '9')
			{
				body_len =
					body_len * 10u + (size_t) (*cl - '0');
				cl++;
			}
			return body_len;
		}
	}
	return 0;
}

static void
serve_one(int conn_fd, echo_handler_t handler)
{
	uint8_t req[8192];
	size_t req_len = 0;
	int hdr_end = -1;
	size_t body_off = 0;
	size_t body_len = 0;
	uint8_t resp_body[4096];
	size_t resp_len = 0;
	int status = 200;
	char head[256];
	int n;
	const char *reason;

	/* Read until we have the full request: \r\n\r\n + body bytes
	 * per Content-Length. Bounded by buffer size. */
	while (req_len < sizeof(req) - 1)
	{
		ssize_t got = recv(
			conn_fd, req + req_len, sizeof(req) - 1 - req_len, 0);
		if (got <= 0)
			break;
		req_len += (size_t) got;

		if (hdr_end < 0)
		{
			hdr_end = find_substring(req, req_len, "\r\n\r\n");
			if (hdr_end >= 0)
			{
				body_off = (size_t) hdr_end + 4;
				body_len = parse_content_length(
					req, (size_t) hdr_end);
			}
		}
		if (hdr_end >= 0 && req_len >= body_off + body_len)
			break; /* full request received */
	}
	if (hdr_end < 0)
		return;

	if (handler)
		status = handler(req + body_off,
			body_len,
			resp_body,
			sizeof(resp_body),
			&resp_len);

	reason = (status == 200) ? "OK" : "Error";
	n = snprintf(head,
		sizeof(head),
		"HTTP/1.1 %d %s\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n"
		"\r\n",
		status,
		reason,
		resp_len);
	(void) send(conn_fd, head, (size_t) n, 0);
	if (resp_len > 0)
		(void) send(conn_fd, resp_body, resp_len, 0);
}

static void *
echo_thread_main(void *arg)
{
	struct echo_thread_arg *a = arg;
	struct echo_server *s = a->server;

	for (;;)
	{
		int conn_fd = accept(s->sock_fd, NULL, NULL);

		if (conn_fd < 0)
		{
			if (errno == EINTR)
				continue;
			break;  /* socket closed by _stop, or fatal */
		}
		serve_one(conn_fd, s->handler);
		close(conn_fd);
		otlp_atomic_fetch_add_u64(&s->requests_served, 1,
			OTLP_MEMORY_ORDER_RELAXED);
		otlp_atomic_store_u64(&s->requests_seen,
			otlp_atomic_load_u64(&s->requests_served,
				OTLP_MEMORY_ORDER_RELAXED),
			OTLP_MEMORY_ORDER_RELAXED);
		if (s->requests_to_serve > 0 &&
		    otlp_atomic_load_u64(&s->requests_served,
				OTLP_MEMORY_ORDER_RELAXED) >= s->requests_to_serve)
			break;
	}
	if (s->sock_fd >= 0)
		close(s->sock_fd);
	s->sock_fd = -1;
	/* RELEASE store: every request-count increment above is visible to
	 * any thread that observes running == 0 via ACQUIRE load. */
	otlp_atomic_store_int(&s->running, 0, OTLP_MEMORY_ORDER_RELEASE);
	free(a);
	return NULL;
}
