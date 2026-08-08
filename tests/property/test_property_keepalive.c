/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property tests for HTTP keep-alive + connection reuse.
 *
 *   prop_keepalive_disabled_on_explicit_close — response with
 *     `Connection: close` makes detach return NULL.
 *   prop_keepalive_eligible_by_default        — response without
 *     Connection header (HTTP/1.1 default) allows detach.
 *   prop_keepalive_reuse_roundtrip            — detach + donate socket
 *     to a second request; verify it completes.
 *
 * Uses a tiny inline TCP server (no echo helper) so we control the
 * exact response bytes — including whether Connection: close is sent.
 *
 * `requests_served` is atomic: the server thread increments it
 * BEFORE send() returns, so by the time the main thread's recv()
 * delivers the response, the counter has already been bumped (with
 * at least release ordering, observed by main's relaxed load). TSAN
 * is satisfied because every cross-thread access goes through atomics.
 */
#include "prng.h"
#include "property_harness.h"
#include "../src/atomic_compat.h"
#include "../src/http_client.h"
#include "../src/platform.h"

#include <otlp-c/status.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

struct mini_srv {
	int listen_fd;
	uint16_t port;
	bool send_connection_close;
	otlp_atomic_int requests_served;
};

static int
mini_start(struct mini_srv *s, bool send_connection_close)
{
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);

	memset(s, 0, sizeof(*s));
	s->send_connection_close = send_connection_close;
	s->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (s->listen_fd < 0)
		return -1;
	{
		int yes = 1;
		(void) setsockopt(s->listen_fd, SOL_SOCKET, SO_REUSEADDR,
				  &yes, sizeof(yes));
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;
	if (bind(s->listen_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		close(s->listen_fd);
		return -1;
	}
	if (getsockname(s->listen_fd, (struct sockaddr *) &addr, &alen) < 0) {
		close(s->listen_fd);
		return -1;
	}
	s->port = ntohs(addr.sin_port);
	if (listen(s->listen_fd, 1) < 0) {
		close(s->listen_fd);
		return -1;
	}
	return 0;
}

static void
mini_stop(struct mini_srv *s)
{
	if (s->listen_fd >= 0) {
		shutdown(s->listen_fd, SHUT_RDWR);
		close(s->listen_fd);
		s->listen_fd = -1;
	}
}

/* Accept one connection, serve N requests on it (keep-alive). Each
 * request gets a fixed 200 response. */
static void *
mini_serve_thread(void *arg)
{
	struct mini_srv *s = arg;
	int conn_fd = accept(s->listen_fd, NULL, NULL);

	if (conn_fd < 0)
		return NULL;
	/* Loop: serve requests on this connection until we hit the cap
	 * (set by parent test via requests_served) or the peer closes. */
	for (int i = 0; i < 4; i++) {
		uint8_t req[1024];
		ssize_t n;
		const char *resp;
		char buf[256];
		int resp_len;

		n = recv(conn_fd, req, sizeof(req), 0);
		if (n <= 0)
			break;
		if (s->send_connection_close)
			resp = "HTTP/1.1 200 OK\r\n"
			       "Content-Length: 2\r\n"
			       "Connection: close\r\n"
			       "\r\n"
			       "hi";
		else
			resp = "HTTP/1.1 200 OK\r\n"
			       "Content-Length: 2\r\n"
			       "\r\n"
			       "hi";
		resp_len = (int) strlen(resp);
		(void) resp_len;
		snprintf(buf, sizeof(buf),
			 "HTTP/1.1 200 OK\r\n"
			 "Content-Length: 2\r\n"
			 "%s"
			 "\r\n"
			 "hi",
			 s->send_connection_close
				? "Connection: close\r\n"
				: "");
		(void) resp;
		/* Bump BEFORE send: once main's recv() returns the response,
		 * the counter has already advanced. RELEASE so main's relaxed
		 * load is well-defined. */
		otlp_atomic_fetch_add_int(&s->requests_served, 1,
			OTLP_MEMORY_ORDER_RELEASE);
		if (send(conn_fd, buf, strlen(buf), 0) < 0)
			break;
		if (s->send_connection_close) {
			close(conn_fd);
			return NULL;
		}
	}
	close(conn_fd);
	return NULL;
}

static otlp_status_t
drive_to_done(otlp_http_request_t *req)
{
	for (int i = 0; i < 5000; i++) {
		otlp_status_t st = otlp_http_request_step(req);
		otlp_http_req_state_t s;

		if (st != OTLP_OK && st != OTLP_ERR_WOULDBLOCK)
			return st;
		s = otlp_http_request_state(req);
		if (s == OTLP_HTTP_REQ_DONE)
			return OTLP_OK;
		if (s == OTLP_HTTP_REQ_FAILED)
			return OTLP_ERR_NETWORK;
		struct timespec ts = { 0, 100 * 1000 };
		nanosleep(&ts, NULL);
	}
	return OTLP_ERR_TIMEOUT;
}

static int
prop_keepalive_disabled_on_explicit_close(uint64_t seed)
{
	struct mini_srv srv;
	pthread_t tid;
	struct otlp_http_url url;
	char url_str[128];
	otlp_http_request_t *req = NULL;
	otlp_socket_t	      *sock;
	int		       ok = 0;

	(void) seed;
	if (mini_start(&srv, true) != 0)
		return 0;
	pthread_create(&tid, NULL, mini_serve_thread, &srv);
	snprintf(url_str, sizeof(url_str), "http://127.0.0.1:%u/", srv.port);
	if (otlp_http_parse_url(url_str, &url) != OTLP_OK)
		goto out;
	if (otlp_http_request_start(&req, &url, "test",
	    (const uint8_t *) "x", 1, 0, 0) != OTLP_OK)
		goto out;
	if (drive_to_done(req) != OTLP_OK)
		goto out;
	sock = otlp_http_request_detach_socket(req);
	ok = (sock == NULL);  /* NULL because Connection: close was sent */
	if (sock)
		otlp_socket_close(sock);
out:
	if (req)
		otlp_http_request_free(req);
	pthread_join(tid, NULL);
	mini_stop(&srv);
	return ok;
}

static int
prop_keepalive_eligible_by_default(uint64_t seed)
{
	struct mini_srv srv;
	pthread_t tid;
	struct otlp_http_url url;
	char url_str[128];
	otlp_http_request_t *req = NULL;
	otlp_socket_t	      *sock;
	int		       ok = 0;

	(void) seed;
	if (mini_start(&srv, false) != 0)
		return 0;
	pthread_create(&tid, NULL, mini_serve_thread, &srv);
	snprintf(url_str, sizeof(url_str), "http://127.0.0.1:%u/", srv.port);
	if (otlp_http_parse_url(url_str, &url) != OTLP_OK)
		goto out;
	if (otlp_http_request_start(&req, &url, "test",
	    (const uint8_t *) "x", 1, 0, 0) != OTLP_OK)
		goto out;
	if (drive_to_done(req) != OTLP_OK)
		goto out;
	sock = otlp_http_request_detach_socket(req);
	ok = (sock != NULL);  /* non-NULL because no Connection: close */
	if (sock)
		otlp_socket_close(sock);
out:
	if (req)
		otlp_http_request_free(req);
	pthread_join(tid, NULL);
	mini_stop(&srv);
	return ok;
}

static int
prop_keepalive_reuse_roundtrip(uint64_t seed)
{
	struct mini_srv srv;
	pthread_t tid;
	struct otlp_http_url url;
	char url_str[128];
	otlp_http_request_t *req1 = NULL, *req2 = NULL;
	otlp_socket_t	      *sock = NULL;
	const uint8_t      *body2;
	size_t	       len2;
	int		       ok = 0;

	(void) seed;
	if (mini_start(&srv, false) != 0)
		return 0;
	pthread_create(&tid, NULL, mini_serve_thread, &srv);
	snprintf(url_str, sizeof(url_str), "http://127.0.0.1:%u/", srv.port);
	if (otlp_http_parse_url(url_str, &url) != OTLP_OK)
		goto out;
	if (otlp_http_request_start(&req1, &url, "test",
	    (const uint8_t *) "first", 5, 0, 0) != OTLP_OK)
		goto out;
	if (drive_to_done(req1) != OTLP_OK)
		goto out;
	sock = otlp_http_request_detach_socket(req1);
	if (!sock)
		goto out;
	if (otlp_http_request_start_with_socket(&req2, &url, "test",
	    (const uint8_t *) "second", 6, 0, 0, sock) != OTLP_OK) {
		req2 = NULL;
		sock = NULL;
		goto out;
	}
	sock = NULL;
	if (drive_to_done(req2) != OTLP_OK)
		goto out;
	body2 = otlp_http_request_body(req2, &len2);
	ok = (len2 == 2 && memcmp(body2, "hi", 2) == 0 &&
	      otlp_atomic_load_int(&srv.requests_served,
		  OTLP_MEMORY_ORDER_RELAXED) == 2);
out:
	if (req1)
		otlp_http_request_free(req1);
	if (req2)
		otlp_http_request_free(req2);
	if (sock)
		otlp_socket_close(sock);
	pthread_join(tid, NULL);
	mini_stop(&srv);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_keepalive_disabled_on_explicit_close,
				 "prop_keepalive_disabled_on_explicit_close", 5, 1);
	failures += property_run(prop_keepalive_eligible_by_default,
				 "prop_keepalive_eligible_by_default", 5, 1);
	failures += property_run(prop_keepalive_reuse_roundtrip,
				 "prop_keepalive_reuse_roundtrip", 5, 1);

	if (failures)
		printf("[property] %d keepalive property(ies) failed\n", failures);
	else
		printf("[property] all keepalive properties passed\n");
	return failures ? 1 : 0;
}
