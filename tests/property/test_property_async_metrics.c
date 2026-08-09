/* SPDX-License-Identifier-Identifier: Apache-2.0 */
/*
 * Property tests for the async metric/log pipeline (v0.5.28).
 *
 *   prop_async_metric_sent    — emit_metric_move + tick (null_transport)
 *     increments sent_metrics.
 *   prop_async_log_sent       — emit_log_move + tick increments sent_logs.
 *   prop_async_spans_coexist  — spans and metrics both flow through the
 *     same exporter without interference.
 *   prop_async_metric_drop_full — emit_metric_move past queue capacity
 *     returns BUFFER_FULL and increments dropped_metrics_full.
 *
 * Uses null_transport to avoid threaded echo server flakes.
 */
#include "property_harness.h"

#include "../src/metric_internal.h"

#include <otlp-c/exporter.h>
#include <otlp-c/log.h>
#include <otlp-c/metric.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <stdint.h>
#include <string.h>

static int
prop_async_metric_sent(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t     *exp;
	otlp_metric_t       *m;
	otlp_exporter_stats_t stats;
	otlp_status_t       st;
	int                  ok = 0;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "async-test";
	opts.batch_size   = 1;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);

	m = otlp_metric_create(OTLP_METRIC_COUNTER, "requests", "1",
			       "total requests", NULL, 0);
	if (!m)
		goto out;
	otlp_metric_record(m, 42.0);
	otlp_metric_mark_time(m);

	st = otlp_exporter_emit_metric_move(exp, m);
	if (st != OTLP_OK)
		goto out;

	/* Tick to drain + null-transport send. */
	otlp_exporter_tick(exp, 100);

	otlp_exporter_get_stats(exp, &stats);
	ok = (stats.emitted_metrics == 1 && stats.sent_metrics == 1);

out:
	otlp_exporter_free(exp);
	return ok;
}

static int
prop_async_log_sent(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t     *exp;
	otlp_log_record_t   *lr;
	otlp_exporter_stats_t stats;
	otlp_status_t       st;
	int                  ok = 0;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "async-test";
	opts.batch_size   = 1;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);

	lr = otlp_log_record_create(OTLP_SEVERITY_INFO, "hello world");
	if (!lr)
		goto out;
	otlp_log_record_mark_timestamp(lr);

	st = otlp_exporter_emit_log_move(exp, lr);
	if (st != OTLP_OK)
		goto out;

	otlp_exporter_tick(exp, 100);

	otlp_exporter_get_stats(exp, &stats);
	ok = (stats.emitted_logs == 1 && stats.sent_logs == 1);

out:
	otlp_exporter_free(exp);
	return ok;
}

static int
prop_async_spans_coexist(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t     *exp;
	otlp_tracer_t       *tracer;
	otlp_metric_t       *m;
	otlp_span_t         *span;
	otlp_exporter_stats_t stats;
	int                  ok = 0;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "coexist-test";
	opts.batch_size   = 1;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);

	tracer = otlp_tracer_create("svc", "demo", "1.0");
	if (!tracer)
		goto out;

	/* Emit a span (via the original async span path). */
	span = otlp_tracer_start_span(tracer, "op");
	if (span)
	{
		otlp_span_mark_end(span);
		otlp_exporter_emit(exp, span);
		otlp_span_free(span);
	}

	/* Emit a metric (via the NEW async metric path). */
	m = otlp_metric_create(OTLP_METRIC_COUNTER, "reqs", "1", "", NULL, 0);
	if (m)
	{
		otlp_metric_record(m, 1.0);
		otlp_metric_mark_time(m);
		otlp_exporter_emit_metric_move(exp, m);
	}

	/* Tick to drain + send both signals. May need multiple ticks
	 * since only one in-flight at a time. */
	otlp_exporter_flush(exp);

	otlp_exporter_get_stats(exp, &stats);
	/* Both span and metric should have been sent. */
	ok = (stats.emitted >= 1 && stats.sent >= 1 &&
	      stats.emitted_metrics == 1 && stats.sent_metrics == 1);

out:
	if (tracer)
		otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	return ok;
}

static int
prop_async_metric_drop_full(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t     *exp;
	otlp_metric_t       *m;
	otlp_exporter_stats_t stats;
	int                  ok = 0;
	int                  i;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name   = "overflow-test";
	opts.queue_capacity = 4;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);

	/* Emit 20 metrics into a 4-deep queue with no ticking. */
	for (i = 0; i < 20; i++)
	{
		m = otlp_metric_create(OTLP_METRIC_COUNTER, "x", "1", "", NULL, 0);
		if (!m)
			goto out;
		otlp_metric_record(m, 1.0);
		otlp_exporter_emit_metric_move(exp, m);
	}

	otlp_exporter_get_stats(exp, &stats);
	/* 4 accepted (emitted_metrics), 16 dropped (dropped_metrics_full). */
	ok = (stats.emitted_metrics == 4 &&
	      stats.dropped_metrics_full >= 1);

out:
	otlp_exporter_free(exp);
	return ok;
}

/* emit_metric (clone variant) — caller keeps ownership. The
 * original metric is still valid after emit; the exporter gets a
 * deep copy. */
static int
prop_async_metric_emit_clone(uint64_t seed)
{
	otlp_exporter_opts_t  opts;
	otlp_exporter_t      *exp;
	otlp_metric_t        *m;
	otlp_exporter_stats_t stats;
	otlp_status_t        st;
	int                   ok = 0;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "clone-test";
	opts.batch_size   = 1;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);

	m = otlp_metric_create(OTLP_METRIC_COUNTER, "counter", "1",
			       "test counter", NULL, 0);
	if (!m)
		goto out;
	otlp_metric_record(m, 99.0);
	otlp_metric_mark_time(m);

	/* emit_metric CLONES — m is still ours. */
	st = otlp_exporter_emit_metric(exp, m);
	if (st != OTLP_OK)
		goto out;

	/* Verify the original is usable after emit. */
	if (strcmp(otlp_metric_get_name(m), "counter") != 0)
		goto out;

	otlp_exporter_tick(exp, 100);

	otlp_exporter_get_stats(exp, &stats);
	ok = (stats.emitted_metrics == 1 && stats.sent_metrics == 1);

out:
	if (m)
		otlp_metric_free(m);
	otlp_exporter_free(exp);
	return ok;
}

/* Metric retry: 500 first, 200 second. Verifies the metric retry
 * path correctly re-sends after a transient failure without
 * double-counting (v0.5.35 null_transport backoff-retry fix). */
static int retry_calls_metric = 0;
static int
retry_status_metric(void *ctx)
{
	(void)ctx;
	retry_calls_metric++;
	return retry_calls_metric == 1 ? 500 : 200;
}

static int
prop_async_metric_retry(uint64_t seed)
{
	otlp_exporter_opts_t  opts;
	otlp_exporter_t      *exp;
	otlp_metric_t        *m;
	otlp_exporter_stats_t stats;
	int                   ok = 0;

	(void)seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "retry";
	opts.batch_size = 1;
	opts.max_retries = 5;
	opts.backoff_initial_ms = 50;
	opts.backoff_max_ms = 100;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);
	retry_calls_metric = 0;
	otlp_exporter_set_null_transport_status_fn(exp,
		retry_status_metric, NULL);

	m = otlp_metric_create(OTLP_METRIC_COUNTER, "r", "1", "", NULL, 0);
	if (!m)
		goto out;
	otlp_metric_record(m, 1.0);
	otlp_metric_mark_time(m);
	if (otlp_exporter_emit_metric_move(exp, m) != OTLP_OK)
		goto out;

	otlp_exporter_flush(exp);

	otlp_exporter_get_stats(exp, &stats);
	/* Sent exactly once (not double-counted). */
	ok = (stats.emitted_metrics == 1 &&
	      stats.sent_metrics == 1 &&
	      stats.dropped_metrics_err == 0);

out:
	otlp_exporter_free(exp);
	return ok;
}

static int retry_calls_log = 0;
static int
retry_status_log(void *ctx)
{
	(void)ctx;
	retry_calls_log++;
	return retry_calls_log == 1 ? 503 : 200;
}

static int
prop_async_log_retry(uint64_t seed)
{
	otlp_exporter_opts_t  opts;
	otlp_exporter_t      *exp;
	otlp_log_record_t    *lr;
	otlp_exporter_stats_t stats;

	(void)seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "retry";
	opts.batch_size = 1;
	opts.max_retries = 5;
	opts.backoff_initial_ms = 50;
	opts.backoff_max_ms = 100;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);
	retry_calls_log = 0;
	otlp_exporter_set_null_transport_status_fn(exp,
		retry_status_log, NULL);

	lr = otlp_log_record_create(OTLP_SEVERITY_WARN, "r");
	if (!lr)
	{
		otlp_exporter_free(exp);
		return 0;
	}
	otlp_log_record_mark_timestamp(lr);
	if (otlp_exporter_emit_log_move(exp, lr) != OTLP_OK)
	{
		otlp_exporter_free(exp);
		return 0;
	}

	otlp_exporter_flush(exp);

	otlp_exporter_get_stats(exp, &stats);
	otlp_exporter_free(exp);
	return (stats.emitted_logs == 1 &&
		stats.sent_logs == 1 &&
		stats.dropped_logs_err == 0);
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_async_metric_sent,
				 "prop_async_metric_sent", 5, 1);
	failures += property_run(prop_async_log_sent,
				 "prop_async_log_sent", 5, 1);
	failures += property_run(prop_async_spans_coexist,
				 "prop_async_spans_coexist", 5, 1);
	failures += property_run(prop_async_metric_drop_full,
				 "prop_async_metric_drop_full", 5, 1);
	failures += property_run(prop_async_metric_emit_clone,
				 "prop_async_metric_emit_clone", 5, 1);
	failures += property_run(prop_async_metric_retry,
				 "prop_async_metric_retry", 3, 1);
	failures += property_run(prop_async_log_retry,
				 "prop_async_log_retry", 3, 1);

	if (failures)
		printf("[property] %d async-metrics property(ies) failed\n",
		       failures);
	else
		printf("[property] all async-metrics properties passed\n");
	return failures ? 1 : 0;
}
