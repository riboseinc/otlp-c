/* SPDX-License-Identifier: Apache-2.0 */
/*
 * otlp_exporter_poll_fds() integration test (v0.5.105). The
 * event-loop surface was exported since v0.1 with ZERO coverage:
 * this drives a REAL poll() loop off the fds + interest bits the
 * exporter exposes, against the echo server, to completion.
 */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test_helper_echo.h"
#include "test_util.h"

#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/status.h>
#include <otlp-c/tracer.h>

#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
int
main(void)
{
	printf("[poll] skipped on Windows\n");
	return 0;
}
#else

static uint64_t
mono_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000ULL +
		(uint64_t) ts.tv_nsec / 1000000ULL;
}

static int
test_no_request_in_flight(void)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	otlp_poll_fd_t fds[2];
	size_t n = 99;
	otlp_status_t st;

	memset(&opts, 0, sizeof(opts));
	opts.endpoint = "http://127.0.0.1:0/v1/traces";
	exp = otlp_exporter_create(&opts);
	check_true(exp != NULL);

	st = otlp_exporter_poll_fds(exp, fds, 2, &n);
	check_true(st == OTLP_OK);
	check_true(n == 0);
	st = otlp_exporter_poll_fds(exp, fds, 0, &n);
	check_true(st == OTLP_OK);
	check_true(n == 0);
	st = otlp_exporter_poll_fds(NULL, fds, 2, &n);
	check_true(st == OTLP_ERR_NULL);
	st = otlp_exporter_poll_fds(exp, NULL, 2, &n);
	check_true(st == OTLP_ERR_NULL);
	st = otlp_exporter_poll_fds(exp, fds, 2, NULL);
	check_true(st == OTLP_ERR_NULL);

	otlp_exporter_free(exp);
	return 0;
}

/* The contract that matters: while a POST is in flight, the fd +
 * interest bits the exporter exposes are exactly what a caller's
 * poll() loop needs to drive the request to completion. */
static int
test_poll_loop_drives_request(void)
{
	struct echo_server srv;
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	otlp_exporter_stats_t stats;
	char endpoint[128];
	otlp_status_t st;
	otlp_poll_fd_t fds[1];
	size_t n = 0;
	uint64_t deadline;

	st = echo_server_start(&srv, NULL, 1);
	check_true(st == OTLP_OK);
	snprintf(endpoint,
		sizeof(endpoint),
		"http://127.0.0.1:%u/v1/traces",
		srv.port);

	memset(&opts, 0, sizeof(opts));
	opts.endpoint = endpoint;
	opts.service_name = "poll-test";
	opts.batch_size = 1;
	opts.batch_ms = 5000;
	exp = otlp_exporter_create(&opts);
	check_true(exp != NULL);
	tracer = otlp_tracer_create("poll-test", "t", "1");
	check_true(tracer != NULL);
	{
		otlp_span_t *s = otlp_tracer_start_span(tracer, "op");

		check_true(s != NULL);
		otlp_span_mark_end(s);
		st = otlp_exporter_emit_move(exp, s);
		check_true(st == OTLP_OK);
	}

	/* One tick starts the POST (batch_size == 1). */
	otlp_exporter_tick(exp, 0);
	st = otlp_exporter_poll_fds(exp, fds, 1, &n);
	check_true(st == OTLP_OK);
	check_true(n == 1);
	check_true(fds[0].fd >= 0);
	check_true(fds[0].events == 1 /* POLLIN */ ||
		fds[0].events == 2 /* POLLOUT */);

	/* The caller's event loop: poll on the exposed fd, tick on
	 * event, until the request completes. Wall-clock bounded. */
	deadline = mono_ms() + 5000;
	while (mono_ms() < deadline)
	{
		struct pollfd pfd;
		int rc;

		pfd.fd = fds[0].fd;
		pfd.events = (short) fds[0].events;
		pfd.revents = 0;
		rc = poll(&pfd, 1, 100);
		check_true(rc >= 0);
		if (rc > 0)
			otlp_exporter_tick(exp, 0);
		else
			otlp_exporter_tick(exp, 0);
		st = otlp_exporter_poll_fds(exp, fds, 1, &n);
		check_true(st == OTLP_OK);
		if (n == 0)
			break; /* request reached a terminal state */
		otlp_exporter_get_stats(exp, &stats);
		if (stats.sent > 0)
			break;
	}

	otlp_exporter_get_stats(exp, &stats);
	check_true(stats.emitted == 1);
	check_true(stats.sent == 1);
	check_true(stats.http_2xx == 1);

	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	st = echo_server_join(&srv, 2 * 1000 * 1000);
	check_true(st == OTLP_OK);
	echo_server_stop(&srv);
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_no_request_in_flight();
	failures += test_poll_loop_drives_request();

	if (failures)
		printf("[poll] FAIL (%d test(s))\n", failures);
	else
		printf("[poll] PASS (2 tests)\n");
	return failures ? 1 : 0;
}

#endif
