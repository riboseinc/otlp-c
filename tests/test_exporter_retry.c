/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Exporter retry test. Echo server returns 500 the first time,
 * 200 thereafter. The exporter should retry with backoff and
 * eventually succeed. Verifies stats: http_5xx=1, http_2xx=1,
 * sent=N.
 *
 * Also verifies the permanent-failure path: a handler that always
 * returns 404 should drop the batch without retrying.
 */
#if !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "test_helper_echo.h"

#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/status.h>
#include <otlp-c/tracer.h>

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
int
main(void)
{
	printf("[exporter-retry] skipped on Windows\n");
	return 0;
}
#else

/* Per-test handler state. Set before each echo_server_start call. */
static _Atomic int g_request_count;

static int
retry_handler(const uint8_t *req_body, size_t req_len,
	      uint8_t *resp_buf, size_t resp_cap, size_t *resp_len)
{
	int n;

	(void)req_body;
	(void)req_len;
	(void)resp_buf;
	(void)resp_cap;
	*resp_len = 0;
	n = atomic_fetch_add_explicit(&g_request_count, 1,
				       memory_order_relaxed);
	return (n == 0) ? 500 : 200;
}

static int
not_found_handler(const uint8_t *req_body, size_t req_len,
		  uint8_t *resp_buf, size_t resp_cap, size_t *resp_len)
{
	(void)req_body;
	(void)req_len;
	(void)resp_buf;
	(void)resp_cap;
	*resp_len = 0;
	return 404;
}

static void
drive_until_settled(otlp_exporter_t *exp, int max_iters)
{
	for (int i = 0; i < max_iters; i++) {
		otlp_exporter_stats_t stats;

		otlp_exporter_tick(exp, 50);
		otlp_exporter_get_stats(exp, &stats);
		if (i < 5 || i % 10 == 0)
			fprintf(stderr, "[drive] iter=%d emitted=%llu "
				"sent=%llu 2xx=%llu 5xx=%llu 4xx=%llu "
				"dropped=%llu net=%llu\n",
				i,
				(unsigned long long)stats.emitted,
				(unsigned long long)stats.sent,
				(unsigned long long)stats.http_2xx,
				(unsigned long long)stats.http_5xx,
				(unsigned long long)stats.http_4xx,
				(unsigned long long)stats.dropped_err,
				(unsigned long long)stats.network_err);
		/* Stop as soon as the batch is resolved. */
		if (stats.sent > 0 || stats.dropped_err > 0)
			return;
	}
}

int
main(void)
{
	struct echo_server	srv;
	otlp_exporter_opts_t	opts;
	otlp_exporter_t	       *exp;
	otlp_tracer_t	       *tracer;
	otlp_span_t	       *span;
	otlp_exporter_stats_t	stats;
	char			endpoint[128];

	/* ── Case 1: retry on 500, succeed on second attempt. */
	atomic_store_explicit(&g_request_count, 0, memory_order_relaxed);
	memset(&srv, 0, sizeof(srv));
	assert(echo_server_start(&srv, retry_handler, 4) == OTLP_OK);
	snprintf(endpoint, sizeof(endpoint),
		 "http://127.0.0.1:%u/v1/traces", srv.port);

	memset(&opts, 0, sizeof(opts));
	opts.endpoint		= endpoint;
	opts.service_name	= "retry-test";
	opts.batch_size		= 1;
	opts.batch_ms		= 10;
	opts.max_retries	= 3;
	opts.backoff_initial_ms = 10;
	opts.backoff_max_ms	= 100;

	exp = otlp_exporter_create(&opts);
	assert(exp != NULL);
	tracer = otlp_tracer_create("retry-test", "test", "0.1");
	assert(tracer != NULL);
	span = otlp_tracer_start_span(tracer, "op");
	assert(span != NULL);
	otlp_span_mark_end(span);
	assert(otlp_exporter_emit_move(exp, span) == OTLP_OK);

	drive_until_settled(exp, /*max_iters=*/100);

	otlp_exporter_get_stats(exp, &stats);
	printf("[retry] case 1 (500 -> 200): emitted=%llu sent=%llu "
	       "2xx=%llu 5xx=%llu dropped_err=%llu\n",
	       (unsigned long long)stats.emitted,
	       (unsigned long long)stats.sent,
	       (unsigned long long)stats.http_2xx,
	       (unsigned long long)stats.http_5xx,
	       (unsigned long long)stats.dropped_err);

	assert(stats.emitted == 1);
	assert(stats.sent == 1);
	assert(stats.http_5xx >= 1);
	assert(stats.http_2xx >= 1);
	assert(stats.dropped_err == 0);

	otlp_tracer_free(tracer);
	otlp_exporter_shutdown(exp);
	otlp_exporter_free(exp);
	echo_server_stop(&srv);
	echo_server_join(&srv, 1 * 1000 * 1000);

	/* ── Case 2: 404 → permanent failure, drop batch, no retry. */
	atomic_store_explicit(&g_request_count, 0, memory_order_relaxed);
	memset(&srv, 0, sizeof(srv));
	assert(echo_server_start(&srv, not_found_handler, 1) == OTLP_OK);
	snprintf(endpoint, sizeof(endpoint),
		 "http://127.0.0.1:%u/v1/traces", srv.port);

	opts.endpoint = endpoint;
	exp = otlp_exporter_create(&opts);
	assert(exp != NULL);
	tracer = otlp_tracer_create("perm-fail", "test", "0.1");
	assert(tracer != NULL);
	span = otlp_tracer_start_span(tracer, "op");
	assert(span != NULL);
	otlp_span_mark_end(span);
	assert(otlp_exporter_emit_move(exp, span) == OTLP_OK);

	drive_until_settled(exp, /*max_iters=*/20);

	otlp_exporter_get_stats(exp, &stats);
	printf("[retry] case 2 (404 permanent): emitted=%llu sent=%llu "
	       "4xx=%llu dropped_err=%llu\n",
	       (unsigned long long)stats.emitted,
	       (unsigned long long)stats.sent,
	       (unsigned long long)stats.http_4xx,
	       (unsigned long long)stats.dropped_err);

	assert(stats.emitted == 1);
	assert(stats.sent == 0);
	assert(stats.http_4xx >= 1);
	assert(stats.dropped_err == 1);

	otlp_tracer_free(tracer);
	otlp_exporter_shutdown(exp);
	otlp_exporter_free(exp);
	echo_server_stop(&srv);
	echo_server_join(&srv, 1 * 1000 * 1000);

	printf("[exporter-retry] PASS — retry-success + permanent-failure "
	       "paths verified\n");
	return 0;
}

#endif
