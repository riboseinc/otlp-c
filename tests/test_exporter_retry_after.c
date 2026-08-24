/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Retry-After wire test (v0.5.95). Real HTTP responses through the
 * echo helper — the null_transport retry test cannot see headers.
 *
 * Case 1 (server floor honored): first response is 429 with
 * "Retry-After: 1"; backoff_initial_ms=1/max=5000 so the jittered
 * delay is ~0. The retry POST must arrive at the server no earlier
 * than ~1s after the 429 — only Retry-After can produce that gap.
 *
 * Case 2 (capped by backoff_max): "Retry-After: 60" with
 * backoff_max_ms=300. The retry must arrive within ~2s — the clamp
 * prevents a hostile server from stalling exports for 60s.
 *
 * Handler timestamps are plain writes in the server thread; the
 * test reads them only after echo_server_join() observes the
 * worker's RELEASE store of running=0 (happens-before edge, so
 * ThreadSanitizer sees no race).
 */
#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

#include "test_helper_echo.h"
#include "test_util.h"

#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/status.h>
#include <otlp-c/tracer.h>

#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
int
main(void)
{
	printf("[exporter-retry-after] skipped on Windows\n");
	return 0;
}
#else

/* Handler state: which canned response to send + wall-clock ms of
 * each served request. */
static uint64_t g_served_ms[2];
static int g_calls;
static const char *g_first_resp;
static const char *g_second_resp;

static uint64_t
mono_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

static int
retry_after_handler(const uint8_t *req_body,
	size_t req_len,
	uint8_t *resp_buf,
	size_t resp_cap,
	size_t *resp_len)
{
	const char *resp;

	(void) req_body;
	(void) req_len;
	resp = (g_calls == 0) ? g_first_resp : g_second_resp;
	{
		size_t len = strlen(resp);

		check_true(len <= resp_cap);
		memcpy(resp_buf, resp, len);
		*resp_len = len;
	}
	g_served_ms[g_calls] = mono_ms();
	g_calls++;
	return ECHO_RAW_RESPONSE;
}

/* Run one throttled-retry scenario. Returns the measured gap
 * between the throttled response and the retry request (ms), or
 * (uint64_t) -1 on failure. */
static uint64_t
run_scenario(const char *first_resp,
	uint32_t backoff_max_ms,
	uint64_t min_gap_ms,
	uint64_t max_gap_ms)
{
	struct echo_server srv;
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	otlp_span_t *span;
	otlp_exporter_stats_t stats;
	char endpoint[128];
	uint64_t t_start;
	uint64_t deadline;
	uint64_t gap;
	otlp_status_t st;

	g_served_ms[0] = 0;
	g_served_ms[1] = 0;
	g_calls = 0;
	g_first_resp = first_resp;
	g_second_resp = "HTTP/1.1 200 OK\r\n"
			"Content-Length: 0\r\n"
			"Connection: close\r\n"
			"\r\n";

	st = echo_server_start(&srv, retry_after_handler, 2);
	check_true(st == OTLP_OK);
	snprintf(endpoint,
		sizeof(endpoint),
		"http://127.0.0.1:%u/v1/traces",
		srv.port);

	memset(&opts, 0, sizeof(opts));
	opts.endpoint = endpoint;
	opts.service_name = "retry-after-test";
	opts.batch_size = 1;
	opts.batch_ms = 5000;
	opts.max_retries = 3;
	/* Jitter contributes ~nothing: initial=1ms. The observed delay
	 * is therefore Retry-After (case 1) or the backoff_max clamp
	 * (case 2). */
	opts.backoff_initial_ms = 1;
	opts.backoff_max_ms = backoff_max_ms;

	exp = otlp_exporter_create(&opts);
	check_true(exp != NULL);
	tracer = otlp_tracer_create("svc", "test", "1.0");
	check_true(tracer != NULL);
	span = otlp_tracer_start_span(tracer, "op");
	check_true(span != NULL);
	otlp_span_mark_end(span);
	/* Statement + pure assert: the emit must execute even under
	 * Release/NDEBUG (side effects never live inside assert()). */
	st = otlp_exporter_emit_move(exp, span);
	check_true(st == OTLP_OK);

	/* Drive tick until the batch is sent — bounded by wall clock,
	 * not iteration count (Release builds spin far faster). */
	t_start = mono_ms();
	deadline = t_start + 10000;
	for (;;)
	{
		otlp_exporter_stats_t s;

		otlp_exporter_tick(exp, 20);
		otlp_exporter_get_stats(exp, &s);
		if (s.sent > 0 || s.dropped_err > 0)
			break;
		if (mono_ms() > deadline)
			break;
	}

	/* Wait for the server thread to finish (2 requests served) so
	 * the timestamp reads below have a happens-before edge. */
	check_ok(echo_server_join(&srv, 5 * 1000 * 1000));
	echo_server_stop(&srv);

	otlp_exporter_get_stats(exp, &stats);
	printf("[exporter-retry-after] gap=%" PRIu64 "ms "
	       "emitted=%llu sent=%llu 4xx=%llu 2xx=%llu dropped_err=%llu\n",
		(g_served_ms[1] && g_served_ms[0])
			? g_served_ms[1] - g_served_ms[0]
			: (uint64_t) -1,
		(unsigned long long) stats.emitted,
		(unsigned long long) stats.sent,
		(unsigned long long) stats.http_4xx,
		(unsigned long long) stats.http_2xx,
		(unsigned long long) stats.dropped_err);

	gap = (g_served_ms[0] && g_served_ms[1])
		? g_served_ms[1] - g_served_ms[0]
		: (uint64_t) -1;

	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);

	if (stats.emitted != 1 || stats.sent != 1 ||
		stats.http_4xx + stats.http_5xx != 1 || stats.http_2xx != 1 ||
		stats.dropped_err != 0)
		return (uint64_t) -1;
	if (gap < min_gap_ms || gap > max_gap_ms)
		return (uint64_t) -1;
	return gap;
}

int
main(void)
{
	uint64_t gap;

	/* Case 1: Retry-After: 1 is the floor — retry arrives >= ~1s
	 * later (jitter alone would fire in ~1ms). Upper bound 5s
	 * catches a runaway wait. */
	gap = run_scenario("HTTP/1.1 429 Too Many Requests\r\n"
			   "Retry-After: 1\r\n"
			   "Content-Length: 0\r\n"
			   "Connection: close\r\n"
			   "\r\n",
		5000,
		900,
		5000);
	if (gap == (uint64_t) -1)
	{
		printf("[exporter-retry-after] FAIL case 1 "
		       "(server floor not honored)\n");
		return 1;
	}

	/* Case 2: Retry-After: 60 clamped by backoff_max_ms=300 —
	 * retry arrives well under 60s (bounded by 2s). */
	gap = run_scenario("HTTP/1.1 503 Service Unavailable\r\n"
			   "Retry-After: 60\r\n"
			   "Content-Length: 0\r\n"
			   "Connection: close\r\n"
			   "\r\n",
		300,
		200,
		2000);
	if (gap == (uint64_t) -1)
	{
		printf("[exporter-retry-after] FAIL case 2 "
		       "(backoff_max clamp not applied)\n");
		return 1;
	}

	printf("[exporter-retry-after] PASS — Retry-After floor + "
	       "backoff_max clamp verified on the wire\n");
	return 0;
}

#endif
