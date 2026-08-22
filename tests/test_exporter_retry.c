/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Exporter retry test. Uses null_transport mode — no echo server,
 * no threads. Fully deterministic on all platforms.
 *
 * Case 1: status callback returns 500 first, 200 after → exporter
 * retries with backoff and eventually succeeds.
 *
 * Case 2: status callback always returns 404 → permanent failure,
 * batch dropped without retry.
 */
#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/status.h>
#include <otlp-c/tracer.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int
retry_status_fn(void *ctx)
{
	int *calls = ctx;

	(*calls)++;
	return (*calls == 1) ? 500 : 200;
}

static int
not_found_status_fn(void *ctx)
{
	(void) ctx;
	return 404;
}

/* 429 once, then success — the throttled-retry path. */
static int
throttled_status_fn(void *ctx)
{
	int *calls = ctx;

	(*calls)++;
	return (*calls == 1) ? 429 : 200;
}

static int
always_500_status_fn(void *ctx)
{
	(void) ctx;
	return 500;
}

static void
drive_until_settled(otlp_exporter_t *exp, int max_iters)
{
	for (int i = 0; i < max_iters; i++)
	{
		otlp_exporter_stats_t stats;

		otlp_exporter_tick(exp, 50);
		otlp_exporter_get_stats(exp, &stats);
		if (stats.sent > 0 || stats.dropped_err > 0)
			return;
	}
}

int
main(void)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	otlp_span_t *span;
	otlp_exporter_stats_t stats;
	otlp_status_t st;
	int calls;

	/* ── Case 1: retry on 500, succeed on second attempt. */
	calls = 0;
	memset(&opts, 0, sizeof(opts));
	opts.endpoint = "http://127.0.0.1:0/v1/traces";
	opts.service_name = "retry-test";
	opts.batch_size = 1;
	opts.batch_ms = 5000;
	opts.max_retries = 3;
	opts.backoff_initial_ms = 1;
	opts.backoff_max_ms = 100;

	exp = otlp_exporter_create(&opts);
	assert(exp != NULL);
	otlp_exporter_set_null_transport(exp, true);
	otlp_exporter_set_null_transport_status_fn(
		exp, retry_status_fn, &calls);
	tracer = otlp_tracer_create("retry-test", "test", "0.1");
	assert(tracer != NULL);
	span = otlp_tracer_start_span(tracer, "op");
	assert(span != NULL);
	otlp_span_mark_end(span);
	st = otlp_exporter_emit_move(exp, span);
	assert(st == OTLP_OK);

	drive_until_settled(exp, 100);

	otlp_exporter_get_stats(exp, &stats);
	printf("[retry] case 1 (500 -> 200): emitted=%llu sent=%llu "
	       "2xx=%llu 5xx=%llu dropped_err=%llu calls=%d\n",
		(unsigned long long) stats.emitted,
		(unsigned long long) stats.sent,
		(unsigned long long) stats.http_2xx,
		(unsigned long long) stats.http_5xx,
		(unsigned long long) stats.dropped_err,
		calls);

	assert(stats.emitted == 1);
	assert(stats.sent == 1);
	assert(stats.http_5xx >= 1);
	assert(stats.http_2xx >= 1);
	assert(stats.dropped_err == 0);
	assert(calls >= 2);

	otlp_tracer_free(tracer);
	otlp_exporter_shutdown(exp);
	otlp_exporter_free(exp);

	/* ── Case 2: 404 → permanent failure, drop batch, no retry. */
	memset(&opts, 0, sizeof(opts));
	opts.endpoint = "http://127.0.0.1:0/v1/traces";
	opts.service_name = "perm-fail";
	opts.batch_size = 1;
	opts.batch_ms = 5000;
	opts.max_retries = 3;
	opts.backoff_initial_ms = 1;
	opts.backoff_max_ms = 100;

	exp = otlp_exporter_create(&opts);
	assert(exp != NULL);
	otlp_exporter_set_null_transport(exp, true);
	otlp_exporter_set_null_transport_status_fn(
		exp, not_found_status_fn, NULL);
	tracer = otlp_tracer_create("perm-fail", "test", "0.1");
	assert(tracer != NULL);
	span = otlp_tracer_start_span(tracer, "op");
	assert(span != NULL);
	otlp_span_mark_end(span);
	st = otlp_exporter_emit_move(exp, span);
	assert(st == OTLP_OK);

	drive_until_settled(exp, 20);

	otlp_exporter_get_stats(exp, &stats);
	printf("[retry] case 2 (404 permanent): emitted=%llu sent=%llu "
	       "4xx=%llu dropped_err=%llu\n",
		(unsigned long long) stats.emitted,
		(unsigned long long) stats.sent,
		(unsigned long long) stats.http_4xx,
		(unsigned long long) stats.dropped_err);

	assert(stats.emitted == 1);
	assert(stats.sent == 0);
	assert(stats.http_4xx >= 1);
	assert(stats.dropped_err == 1);

	otlp_tracer_free(tracer);
	otlp_exporter_shutdown(exp);
	otlp_exporter_free(exp);

	/* ── Case 3: 429 → retryable, counted in the http_4xx bucket
	 * (its real status class), then succeeds. */
	memset(&opts, 0, sizeof(opts));
	opts.endpoint = "http://127.0.0.1:0/v1/traces";
	opts.service_name = "throttled";
	opts.batch_size = 1;
	opts.batch_ms = 5000;
	opts.max_retries = 3;
	opts.backoff_initial_ms = 1;
	opts.backoff_max_ms = 10;

	calls = 0;
	exp = otlp_exporter_create(&opts);
	assert(exp != NULL);
	otlp_exporter_set_null_transport(exp, true);
	otlp_exporter_set_null_transport_status_fn(
		exp, throttled_status_fn, &calls);
	tracer = otlp_tracer_create("throttled", "test", "0.1");
	assert(tracer != NULL);
	span = otlp_tracer_start_span(tracer, "op");
	assert(span != NULL);
	otlp_span_mark_end(span);
	st = otlp_exporter_emit_move(exp, span);
	assert(st == OTLP_OK);

	drive_until_settled(exp, 40);

	otlp_exporter_get_stats(exp, &stats);
	printf("[retry] case 3 (429 retry): emitted=%llu sent=%llu "
	       "4xx=%llu 5xx=%llu\n",
		(unsigned long long) stats.emitted,
		(unsigned long long) stats.sent,
		(unsigned long long) stats.http_4xx,
		(unsigned long long) stats.http_5xx);

	assert(stats.emitted == 1);
	assert(stats.sent == 1);
	assert(stats.http_4xx >= 1);
	assert(stats.http_5xx == 0);
	assert(stats.dropped_err == 0);

	otlp_tracer_free(tracer);
	otlp_exporter_shutdown(exp);
	otlp_exporter_free(exp);

	/* ── Case 4: max_retries far above the uint32 shift width —
	 * the backoff computation must saturate, not shift-overflow
	 * (CWE-190 family; caught by UBSAN before the v0.5.83 fix). */
	memset(&opts, 0, sizeof(opts));
	opts.endpoint = "http://127.0.0.1:0/v1/traces";
	opts.service_name = "shift-guard";
	opts.batch_size = 1;
	opts.batch_ms = 5000;
	opts.max_retries = 100;
	opts.backoff_initial_ms = 1;
	opts.backoff_max_ms = 1;

	exp = otlp_exporter_create(&opts);
	assert(exp != NULL);
	otlp_exporter_set_null_transport(exp, true);
	otlp_exporter_set_null_transport_status_fn(
		exp, always_500_status_fn, NULL);
	tracer = otlp_tracer_create("shift-guard", "test", "0.1");
	assert(tracer != NULL);
	span = otlp_tracer_start_span(tracer, "op");
	assert(span != NULL);
	otlp_span_mark_end(span);
	st = otlp_exporter_emit_move(exp, span);
	assert(st == OTLP_OK);

	/* 100 retries at <= 1ms jittered delay: settle quickly. */
	drive_until_settled(exp, 400);

	otlp_exporter_get_stats(exp, &stats);
	printf("[retry] case 4 (max_retries=100): emitted=%llu sent=%llu "
	       "dropped_err=%llu\n",
		(unsigned long long) stats.emitted,
		(unsigned long long) stats.sent,
		(unsigned long long) stats.dropped_err);

	assert(stats.emitted == 1);
	assert(stats.sent == 0);
	assert(stats.dropped_err == 1);

	otlp_tracer_free(tracer);
	otlp_exporter_shutdown(exp);
	otlp_exporter_free(exp);

	printf("[exporter-retry] PASS — retry-success + permanent-failure + "
	       "429-bucket + shift-guard paths verified\n");
	return 0;
}
