// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the platform socket layer and tracer edge paths
// — the coverage-guided targets (v0.5.91): NULL guards, DNS
// failure, connection refused.

#include "../../src/platform.h"
#include "../test_util.h"
#include "../../src/span_internal.h"

#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
int
main(void)
{
	printf("[unit-platform] skipped on Windows\n");
	return 0;
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

static int
test_socket_null_guards(void)
{
	size_t n = 0;

	check_true(otlp_socket_connect(NULL, "h", 1) == OTLP_ERR_NULL);
	check_true(otlp_socket_finish_connect(NULL) == OTLP_ERR_NULL);
	check_true(otlp_socket_write(NULL, (const uint8_t *) "x", 1, &n) ==
		OTLP_ERR_NULL);
	check_true(
		otlp_socket_read(NULL, (uint8_t *) &n, 1, &n) == OTLP_ERR_NULL);
	check_true(otlp_socket_eof(NULL) == 0);
	otlp_socket_close(NULL); /* must not crash */
	return 0;
}

static int
test_dns_failure(void)
{
	otlp_socket_t *s = NULL;
	/* .invalid is RFC 2606-reserved: resolution always fails. */
	otlp_status_t st = otlp_socket_connect(&s, "nonexistent.invalid", 80);

	if (st != OTLP_ERR_DNS && st != OTLP_ERR_CONNECT)
		return 1;
	if (s != NULL)
		return 1;
	return 0;
}

static int
test_connect_refused(void)
{
	struct sockaddr_in addr;
	socklen_t alen = sizeof(addr);
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	uint16_t port;
	otlp_socket_t *s = NULL;
	otlp_status_t st;

	if (fd < 0)
		return 1;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0;
	check_true(bind(fd, (struct sockaddr *) &addr, sizeof(addr)) == 0);
	check_true(getsockname(fd, (struct sockaddr *) &addr, &alen) == 0);
	port = ntohs(addr.sin_port);
	close(fd); /* leave the port closed */

	st = otlp_socket_connect(&s, "127.0.0.1", port);
	if (st != OTLP_OK)
		return 0; /* synchronous refusal — also fine */
	/* Non-blocking: drive finish_connect to the refusal. */
	{
		int i;

		for (i = 0; i < 10000; i++)
		{
			st = otlp_socket_finish_connect(s);
			if (st != OTLP_ERR_WOULDBLOCK)
				break;
			usleep(100);
		}
	}
	check_true(st == OTLP_ERR_CONNECT);
	otlp_socket_close(s);
	return 0;
}

static int
test_tracer_edges(void)
{
	otlp_tracer_t *t = otlp_tracer_create("s", "l", "v");
	otlp_span_t *parent;
	otlp_span_t *child;

	check_true(t != NULL);
	otlp_tracer_free(NULL); /* no crash */
	otlp_tracer_set_sampler(NULL, NULL); /* no crash */
	otlp_tracer_set_sampler(t, NULL); /* resets to always_on */

	check_true(otlp_tracer_start_child_span(t, "c", NULL) == NULL);

	parent = otlp_tracer_start_span(t, "p");
	check_true(parent != NULL);
	child = otlp_tracer_start_child_span(t, "c", parent);
	check_true(child != NULL);
	/* Child inherits the parent's trace id. */
	check_true(memcmp(otlp_span_get_trace_id(child),
			   otlp_span_get_trace_id(parent),
			   OTLP_TRACE_ID_LEN) == 0);
	check_true(otlp_span_has_parent(child));
	otlp_span_free(child);
	otlp_span_free(parent);
	otlp_tracer_free(t);
	return 0;
}

static int
test_clock_null_guards(void)
{
	check_true(otlp_platform_now_unix_nano(NULL) == OTLP_ERR_NULL);
	check_true(otlp_platform_now_mono_nano(NULL) == OTLP_ERR_NULL);
	{
		uint64_t t = 0;

		check_ok(otlp_platform_now_unix_nano(&t));
		check_true(t > 0);
		check_ok(otlp_platform_now_mono_nano(&t));
	}
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_socket_null_guards();
	failures += test_clock_null_guards();
	failures += test_dns_failure();
	failures += test_connect_refused();
	failures += test_tracer_edges();

	if (failures)
		printf("[unit-platform] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-platform] PASS (5 tests)\n");
	return failures ? 1 : 0;
}

#endif /* !_WIN32 */
