/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Structured diagnostics events (v0.5.100). Null-transport mode —
 * no echo server, no threads. Fully deterministic on all
 * platforms. Each scenario drives the exporter through a known
 * outcome and asserts the exact event (code, signal, counts,
 * level, drop reason) the new otlp_exporter_set_event_logger
 * callback delivers.
 */
#include "test_util.h"

#include <otlp-c/exporter.h>
#include <otlp-c/log.h>
#include <otlp-c/metric.h>
#include <otlp-c/span.h>
#include <otlp-c/status.h>
#include <otlp-c/tracer.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Event capture ─────────────────────────────────────────────── */

#define MAX_EVENTS 32

static otlp_event_t g_events[MAX_EVENTS];
static size_t g_n_events;
static char g_detail[MAX_EVENTS][128];

static void
capture_event(void *ctx, const otlp_event_t *ev)
{
	(void) ctx;
	if (g_n_events < MAX_EVENTS)
	{
		g_events[g_n_events] = *ev;
		if (ev->detail && ev->detail_len > 0)
		{
			size_t n = ev->detail_len < 127 ? ev->detail_len : 127;

			memcpy(g_detail[g_n_events], ev->detail, n);
			g_detail[g_n_events][n] = '\0';
			g_events[g_n_events].detail = g_detail[g_n_events];
			g_events[g_n_events].detail_len = n;
		}
	}
	g_n_events++;
}

static void
reset_events(void)
{
	g_n_events = 0;
}

static const otlp_event_t *
find_event(otlp_event_code_t code)
{
	size_t i;

	for (i = 0; i < g_n_events && i < MAX_EVENTS; i++)
		if (g_events[i].code == code)
			return &g_events[i];
	return NULL;
}

static size_t
count_events(otlp_event_code_t code)
{
	size_t i, n = 0;

	for (i = 0; i < g_n_events && i < MAX_EVENTS; i++)
		if (g_events[i].code == code)
			n++;
	return n;
}

/* ── Helpers ───────────────────────────────────────────────────── */

/* Drive until a stats/event condition holds or the iteration
 * budget is spent. Portable (no clock): each tick(5) sleeps up
 * to 5ms internally on the platform's own sleep, so 200
 * iterations bounds the wait at ~1s — long after any of these
 * scenarios (backoff caps are 5ms) settles. The condition is
 * checked via stats, not wall time: waiting-on-timeout loops
 * bound by iteration count alone are the flaky pattern; this
 * one exits on the OUTCOME. */
static void
drive_until(otlp_exporter_t *exp,
	const otlp_event_code_t want,
	int want_count,
	int max_iters)
{
	for (int i = 0; i < max_iters; i++)
	{
		otlp_exporter_tick(exp, 5);
		if ((int) count_events(want) >= want_count)
			return;
	}
}

static otlp_exporter_t *
make_exp(unsigned max_retries)
{
	static otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;

	memset(&opts, 0, sizeof(opts));
	opts.endpoint = "http://127.0.0.1:0/v1/traces";
	opts.service_name = "events-test";
	opts.batch_size = 1;
	opts.batch_ms = 5000;
	opts.max_retries = max_retries;
	opts.backoff_initial_ms = 1;
	opts.backoff_max_ms = 5;
	opts.queue_capacity = 4;

	reset_events();
	exp = otlp_exporter_create(&opts);
	check_true(exp != NULL);
	otlp_exporter_set_null_transport(exp, true);
	otlp_exporter_set_event_logger(exp, capture_event, NULL);
	return exp;
}

static otlp_span_t *
make_span(otlp_tracer_t *tracer)
{
	otlp_span_t *span = otlp_tracer_start_span(tracer, "op");

	check_true(span != NULL);
	otlp_span_mark_end(span);
	return span;
}

/* ── Status callbacks ─────────────────────────────────────────── */

static int
status_ok(void *ctx)
{
	(void) ctx;
	return 200;
}

static int
status_500_then_200(void *ctx)
{
	int *calls = ctx;

	(*calls)++;
	return (*calls <= 2) ? 500 : 200;
}

static int
status_always_500(void *ctx)
{
	(void) ctx;
	return 500;
}

static int
status_404(void *ctx)
{
	(void) ctx;
	return 404;
}

/* ── Scenarios ─────────────────────────────────────────────────── */

static int
test_event_batch_sent(void)
{
	otlp_exporter_t *exp = make_exp(3);
	otlp_tracer_t *tracer = otlp_tracer_create("ev", "t", "1");
	otlp_status_t st;
	const otlp_event_t *ev;

	check_true(tracer != NULL);
	otlp_exporter_set_null_transport_status_fn(exp, status_ok, NULL);
	st = otlp_exporter_emit_move(exp, make_span(tracer));
	check_true(st == OTLP_OK);
	drive_until(exp, OTLP_EVT_BATCH_SENT, 1, 200);

	ev = find_event(OTLP_EVT_BATCH_SENT);
	check_true(ev != NULL);
	check_true(ev->signal == OTLP_SIGNAL_TRACES);
	check_true(ev->count == 1);
	check_true(ev->level == OTLP_LOG_DEBUG);
	check_true(ev->http_status == 200);
	check_true(count_events(OTLP_EVT_BATCH_SENT) == 1);

	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	return 0;
}

static int
test_event_batch_sent_signal_ids(void)
{
	otlp_exporter_t *exp = make_exp(3);
	otlp_log_record_t *lr;
	otlp_metric_t *m;
	otlp_status_t st;
	const otlp_event_t *ev;

	otlp_exporter_set_null_transport_status_fn(exp, status_ok, NULL);

	lr = otlp_log_record_create(OTLP_SEVERITY_INFO, "hello");
	check_true(lr != NULL);
	st = otlp_exporter_emit_log_move(exp, lr);
	check_true(st == OTLP_OK);
	drive_until(exp, OTLP_EVT_BATCH_SENT, 1, 200);
	ev = find_event(OTLP_EVT_BATCH_SENT);
	check_true(ev != NULL);
	check_true(ev->signal == OTLP_SIGNAL_LOGS);

	reset_events();
	m = otlp_metric_create(OTLP_METRIC_COUNTER, "c", "1", "d", NULL, 0);
	check_true(m != NULL);
	st = otlp_exporter_emit_metric_move(exp, m);
	check_true(st == OTLP_OK);
	drive_until(exp, OTLP_EVT_BATCH_SENT, 1, 200);
	ev = find_event(OTLP_EVT_BATCH_SENT);
	check_true(ev != NULL);
	check_true(ev->signal == OTLP_SIGNAL_METRICS);

	otlp_exporter_free(exp);
	return 0;
}

static int
test_event_retry_armed(void)
{
	otlp_exporter_t *exp = make_exp(3);
	otlp_tracer_t *tracer = otlp_tracer_create("ev", "t", "1");
	int calls = 0;
	otlp_status_t st;
	const otlp_event_t *ev;

	check_true(tracer != NULL);
	otlp_exporter_set_null_transport_status_fn(
		exp, status_500_then_200, &calls);
	st = otlp_exporter_emit_move(exp, make_span(tracer));
	check_true(st == OTLP_OK);
	drive_until(exp, OTLP_EVT_BATCH_SENT, 1, 200);

	check_true(count_events(OTLP_EVT_RETRY_ARMED) == 2);
	ev = find_event(OTLP_EVT_RETRY_ARMED);
	check_true(ev != NULL);
	check_true(ev->http_status == 500);
	check_true(ev->attempt == 1);
	check_true(ev->max_retries == 3);
	/* Full jitter draws uniformly from [0, delay] — 0 is a
	 * legitimate draw with these tiny test bounds. */
	check_true(ev->delay_ms <= 5);
	check_true(ev->level == OTLP_LOG_WARN);
	check_true(ev->signal == OTLP_SIGNAL_TRACES);
	check_true(find_event(OTLP_EVT_BATCH_SENT) != NULL);
	check_true(find_event(OTLP_EVT_ITEMS_DROPPED) == NULL);

	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	return 0;
}

static int
test_event_dropped_max_retries(void)
{
	otlp_exporter_t *exp = make_exp(1);
	otlp_tracer_t *tracer = otlp_tracer_create("ev", "t", "1");
	otlp_status_t st;
	const otlp_event_t *ev;

	check_true(tracer != NULL);
	otlp_exporter_set_null_transport_status_fn(
		exp, status_always_500, NULL);
	st = otlp_exporter_emit_move(exp, make_span(tracer));
	check_true(st == OTLP_OK);
	drive_until(exp, OTLP_EVT_ITEMS_DROPPED, 1, 200);

	ev = find_event(OTLP_EVT_ITEMS_DROPPED);
	check_true(ev != NULL);
	check_true(ev->drop_reason == OTLP_DROP_MAX_RETRIES);
	check_true(ev->http_status == 500);
	check_true(ev->count == 1);
	check_true(ev->level == OTLP_LOG_ERROR);
	check_true(ev->max_retries == 1);
	check_true(find_event(OTLP_EVT_BATCH_SENT) == NULL);

	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	return 0;
}

static int
test_event_dropped_permanent_4xx(void)
{
	otlp_exporter_t *exp = make_exp(3);
	otlp_tracer_t *tracer = otlp_tracer_create("ev", "t", "1");
	otlp_status_t st;
	const otlp_event_t *ev;

	check_true(tracer != NULL);
	otlp_exporter_set_null_transport_status_fn(exp, status_404, NULL);
	st = otlp_exporter_emit_move(exp, make_span(tracer));
	check_true(st == OTLP_OK);
	drive_until(exp, OTLP_EVT_ITEMS_DROPPED, 1, 200);

	ev = find_event(OTLP_EVT_ITEMS_DROPPED);
	check_true(ev != NULL);
	check_true(ev->drop_reason == OTLP_DROP_HTTP_STATUS);
	check_true(ev->http_status == 404);
	check_true(ev->count == 1);
	check_true(ev->level == OTLP_LOG_ERROR);
	check_true(count_events(OTLP_EVT_RETRY_ARMED) == 0);

	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	return 0;
}

static int
test_event_queue_full(void)
{
	otlp_exporter_t *exp = make_exp(3);
	otlp_tracer_t *tracer = otlp_tracer_create("ev", "t", "1");
	int i, full_rc = 0, n_queued = 0;
	const otlp_event_t *ev;

	check_true(tracer != NULL);
	otlp_exporter_set_null_transport_status_fn(exp, status_ok, NULL);
	/* batch_ms 5000 + no tick: nothing drains; capacity is 4. */
	for (i = 0; i < 8; i++)
	{
		otlp_status_t st =
			otlp_exporter_emit_move(exp, make_span(tracer));

		if (st == OTLP_ERR_BUFFER_FULL)
			full_rc++;
		else
			n_queued++;
	}

	ev = find_event(OTLP_EVT_QUEUE_FULL);
	check_true(ev != NULL);
	check_true(ev->drop_reason == OTLP_DROP_QUEUE_FULL);
	check_true(ev->count == 1);
	check_true(ev->level == OTLP_LOG_WARN);
	check_true(full_rc > 0);
	check_true(full_rc == (int) count_events(OTLP_EVT_QUEUE_FULL));
	check_true(full_rc + n_queued == 8);

	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	return 0;
}

/* One-shot sync flush against a port nothing listens on: with
 * max_retries 0 the single attempt fails fast (connection
 * refused) and must surface as SYNC_FLUSH_FAILED with the metrics
 * signal. Real transport (null_transport skips sync flush). */
static int
test_event_sync_flush_failed(void)
{
	static otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	otlp_metric_t *m;
	otlp_status_t st;
	const otlp_event_t *ev;

	reset_events();
	memset(&opts, 0, sizeof(opts));
	opts.endpoint = "http://127.0.0.1:1/v1/metrics";
	opts.service_name = "ev";
	opts.max_retries = 0;
	opts.connect_timeout_ms = 200;
	opts.read_timeout_ms = 200;
	opts.flush_timeout_ms = 500;

	exp = otlp_exporter_create(&opts);
	check_true(exp != NULL);
	otlp_exporter_set_event_logger(exp, capture_event, NULL);
	m = otlp_metric_create(OTLP_METRIC_COUNTER, "c", "1", "d", NULL, 0);
	check_true(m != NULL);
	st = otlp_exporter_flush_metric(exp, m);
	check_true(st != OTLP_OK);

	ev = find_event(OTLP_EVT_SYNC_FLUSH_FAILED);
	check_true(ev != NULL);
	check_true(ev->signal == OTLP_SIGNAL_METRICS);
	check_true(ev->level == OTLP_LOG_ERROR);
	check_true(ev->status != OTLP_OK);

	otlp_metric_free(m);
	otlp_exporter_free(exp);
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_event_batch_sent();
	failures += test_event_batch_sent_signal_ids();
	failures += test_event_retry_armed();
	failures += test_event_dropped_max_retries();
	failures += test_event_dropped_permanent_4xx();
	failures += test_event_queue_full();
	failures += test_event_sync_flush_failed();

	reset_events();
	if (failures)
		printf("[exporter-events] FAIL (%d test(s))\n", failures);
	else
		printf("[exporter-events] PASS (7 tests)\n");
	return failures ? 1 : 0;
}
