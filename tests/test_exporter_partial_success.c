/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * PartialSuccess wire test (v0.5.96). The OTLP spec lets a
 * collector answer 200 OK while reporting server-side data loss in
 * the response body (Export*PartialSuccess: rejected count +
 * error message). The exporter must surface that — WARN diagnostic
 * + per-signal rejected_* stat — WITHOUT retrying (a 200 is final).
 *
 * Case 1 (spans): 200 + PartialSuccess{rejected=2 of 3, "queue
 * full"} → exactly 1 POST, sent=3, rejected_spans=2, WARN logged
 * with the server's message.
 *
 * Case 2 (logs): same shape through /v1/logs → rejected_logs=1.
 *
 * Case 3 (clean): 200 + empty body → no rejection counted, no WARN.
 *
 * Handler timestamps/state: plain writes in the server thread; the
 * test reads them after echo_server_join() observed the worker's
 * RELEASE exit store (happens-before edge — TSAN-clean).
 */
#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

#include "test_helper_echo.h"
#include "test_util.h"

#include <otlp-c/exporter.h>
#include <otlp-c/log.h>
#include <otlp-c/span.h>
#include <otlp-c/status.h>
#include <otlp-c/tracer.h>

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
int
main(void)
{
	printf("[exporter-partial-success] skipped on Windows\n");
	return 0;
}
#else

/* One canned raw response, served verbatim. */
static const uint8_t *g_body;
static size_t g_body_len;

/* Captured diagnostics. */
static bool g_saw_partial_warn;
static otlp_event_t g_ps_event; /* copy of the PARTIAL_SUCCESS
				 * event, if one fires */
static bool g_saw_ps_event;
static char g_ps_detail[128];

static void
capture_event(void *ctx, const otlp_event_t *ev)
{
	(void) ctx;
	if (ev->code == OTLP_EVT_PARTIAL_SUCCESS)
	{
		g_ps_event = *ev;
		g_saw_ps_event = true;
		if (ev->detail && ev->detail_len > 0)
		{
			size_t n = ev->detail_len < 127 ? ev->detail_len : 127;

			memcpy(g_ps_detail, ev->detail, n);
			g_ps_detail[n] = '\0';
			g_ps_event.detail = g_ps_detail;
			g_ps_event.detail_len = n;
		}
	}
}
static char g_last_msg[256];

static int
raw_200_handler(const uint8_t *req_body,
	size_t req_len,
	uint8_t *resp_buf,
	size_t resp_cap,
	size_t *resp_len)
{
	int n;

	(void) req_body;
	(void) req_len;
	n = snprintf((char *) resp_buf,
		resp_cap,
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/x-protobuf\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n"
		"\r\n",
		g_body_len);
	check_true(n > 0 && (size_t) n + g_body_len <= resp_cap);
	if (g_body_len > 0) /* NULL + 0 memcpy is UB under UBSAN */
		memcpy(resp_buf + n, g_body, g_body_len);
	*resp_len = (size_t) n + g_body_len;
	return ECHO_RAW_RESPONSE;
}

static void
capture_log(void *ctx, otlp_log_level_t level, const char *message)
{
	(void) ctx;
	if (level == OTLP_LOG_WARN && strstr(message, "partial success"))
	{
		g_saw_partial_warn = true;
		snprintf(g_last_msg, sizeof(g_last_msg), "%s", message);
	}
}

/* ── Body builders (hand-assembled protobuf) ──────────────────── */

/* partial_success submessage {rejected=r, error_message=msg}. */
static size_t
build_ps(uint8_t *out, int64_t r, const char *msg)
{
	size_t sub = 0;
	size_t msg_len = strlen(msg);

	out[sub++] = 0x08; /* field 1 varint */
	{
		uint64_t v = (uint64_t) r;

		do
		{
			uint8_t b = v & 0x7f;

			v >>= 7;
			out[sub++] = b | (v ? 0x80 : 0);
		} while (v);
	}
	out[sub++] = 0x12; /* field 2 len */
	out[sub++] = (uint8_t) msg_len;
	memcpy(out + sub, msg, msg_len);
	sub += msg_len;
	return sub;
}

/* Export*ServiceResponse wrapping the submessage in field 5. */
static size_t
build_response_body(uint8_t *out, size_t out_cap, int64_t r, const char *msg)
{
	uint8_t sub[128];
	size_t sub_len = build_ps(sub, r, msg);

	check_true(sub_len < 128 && sub_len + 2 <= out_cap);
	out[0] = 0x2a; /* field 5, LEN */
	out[1] = (uint8_t) sub_len;
	memcpy(out + 2, sub, sub_len);
	return sub_len + 2;
}

/* ── Scenario driver ──────────────────────────────────────────── */

static bool
run_span_scenario(const uint8_t *body,
	size_t body_len,
	size_t n_spans,
	int64_t expect_rejected_stat)
{
	struct echo_server srv;
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	otlp_exporter_stats_t stats;
	char endpoint[128];
	otlp_status_t st;
	bool ok;

	g_body = body;
	g_body_len = body_len;
	g_saw_partial_warn = false;
	g_saw_ps_event = false;
	g_last_msg[0] = '\0';

	/* Exactly ONE POST arrives per scenario (batch_size == the
	 * emitted count), so requests_to_serve=1 makes the worker
	 * exit deterministically — a lingering worker touching `srv`
	 * after this frame returns is stack-use-after-return. */
	st = echo_server_start(&srv, raw_200_handler, 1);
	check_true(st == OTLP_OK);
	snprintf(endpoint,
		sizeof(endpoint),
		"http://127.0.0.1:%u/v1/traces",
		srv.port);

	memset(&opts, 0, sizeof(opts));
	opts.endpoint = endpoint;
	opts.service_name = "partial-success-test";
	opts.batch_size = n_spans;
	opts.batch_ms = 1000;
	exp = otlp_exporter_create(&opts);
	check_true(exp != NULL);
	otlp_exporter_set_logger(exp, capture_log, NULL);
	otlp_exporter_set_event_logger(exp, capture_event, NULL);

	tracer = otlp_tracer_create("svc", "test", "1.0");
	check_true(tracer != NULL);
	for (size_t i = 0; i < n_spans; i++)
	{
		otlp_span_t *s = otlp_tracer_start_span(tracer, "op");

		check_true(s != NULL);
		otlp_span_mark_end(s);
		st = otlp_exporter_emit_move(exp, s);
		check_true(st == OTLP_OK);
	}

	/* flush() drives the pipeline to quiesce (bounded by
	 * flush_timeout_ms). */
	st = otlp_exporter_flush(exp);
	check_true(st == OTLP_OK);

	/* Checked, not assumed: the worker must have exited before
	 * this frame goes away. */
	st = echo_server_join(&srv, 2 * 1000 * 1000);
	check_true(st == OTLP_OK);
	echo_server_stop(&srv);
	otlp_exporter_get_stats(exp, &stats);

	printf("[exporter-partial-success] spans: sent=%llu "
	       "rejected_spans=%llu warn=%d msg='%s'\n",
		(unsigned long long) stats.sent,
		(unsigned long long) stats.rejected_spans,
		g_saw_partial_warn ? 1 : 0,
		g_last_msg);

	ok = stats.emitted == n_spans && stats.sent == n_spans &&
		stats.rejected_spans ==
			(uint64_t)(expect_rejected_stat > 0
					? expect_rejected_stat
					: 0) &&
		g_saw_partial_warn == (expect_rejected_stat > 0);
	if (expect_rejected_stat > 0)
		ok = ok && g_saw_ps_event &&
			g_ps_event.code == OTLP_EVT_PARTIAL_SUCCESS &&
			g_ps_event.signal == OTLP_SIGNAL_TRACES &&
			g_ps_event.level == OTLP_LOG_WARN &&
			g_ps_event.count == n_spans &&
			g_ps_event.rejected ==
				(uint64_t) expect_rejected_stat &&
			strcmp(g_ps_detail, "queue full") == 0;
	else
		ok = ok && !g_saw_ps_event;

	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	return ok;
}

static bool
run_log_scenario(const uint8_t *body, size_t body_len)
{
	struct echo_server srv;
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	otlp_exporter_stats_t stats;
	char endpoint[128];
	otlp_status_t st;
	bool ok;

	g_body = body;
	g_body_len = body_len;
	g_saw_partial_warn = false;
	g_saw_ps_event = false;
	g_last_msg[0] = '\0';

	st = echo_server_start(&srv, raw_200_handler, 1);
	check_true(st == OTLP_OK);
	snprintf(endpoint,
		sizeof(endpoint),
		"http://127.0.0.1:%u/v1/logs",
		srv.port);

	memset(&opts, 0, sizeof(opts));
	opts.endpoint = endpoint;
	opts.service_name = "partial-success-test";
	opts.batch_size = 2;
	opts.batch_ms = 1000;
	exp = otlp_exporter_create(&opts);
	check_true(exp != NULL);
	otlp_exporter_set_logger(exp, capture_log, NULL);
	otlp_exporter_set_event_logger(exp, capture_event, NULL);

	for (int i = 0; i < 2; i++)
	{
		otlp_log_record_t *lr = otlp_log_record_create(
			OTLP_SEVERITY_INFO, "partial success test");

		check_true(lr != NULL);
		st = otlp_exporter_emit_log_move(exp, lr);
		check_true(st == OTLP_OK);
	}
	st = otlp_exporter_flush(exp);
	check_true(st == OTLP_OK);

	/* Checked, not assumed: the worker must have exited before
	 * this frame goes away. */
	st = echo_server_join(&srv, 2 * 1000 * 1000);
	check_true(st == OTLP_OK);
	echo_server_stop(&srv);
	otlp_exporter_get_stats(exp, &stats);

	printf("[exporter-partial-success] logs: sent=%llu "
	       "rejected_logs=%llu\n",
		(unsigned long long) stats.sent_logs,
		(unsigned long long) stats.rejected_logs);

	ok = stats.emitted_logs == 2 && stats.sent_logs == 2 &&
		stats.rejected_logs == 1 && g_saw_partial_warn &&
		g_saw_ps_event && g_ps_event.signal == OTLP_SIGNAL_LOGS &&
		g_ps_event.rejected == 1;

	otlp_exporter_free(exp);
	return ok;
}

int
main(void)
{
	uint8_t body[160];
	size_t body_len;

	/* Case 1: 2 of 3 spans rejected with a reason. */
	body_len = build_response_body(body, sizeof(body), 2, "queue full");
	if (!run_span_scenario(body, body_len, 3, 2))
	{
		printf("[exporter-partial-success] FAIL case 1 (spans)\n");
		return 1;
	}
	if (!strstr(g_last_msg, "queue full"))
	{
		printf("[exporter-partial-success] FAIL case 1 "
		       "(server message not surfaced)\n");
		return 1;
	}

	/* Case 2: logs. */
	body_len = build_response_body(body, sizeof(body), 1, "rate limited");
	if (!run_log_scenario(body, body_len))
	{
		printf("[exporter-partial-success] FAIL case 2 (logs)\n");
		return 1;
	}

	/* Case 3: clean 200, empty body — nothing rejected, no WARN. */
	if (!run_span_scenario(NULL, 0, 2, 0))
	{
		printf("[exporter-partial-success] FAIL case 3 (clean)\n");
		return 1;
	}

	printf("[exporter-partial-success] PASS — spans + logs + clean "
	       "paths verified\n");
	return 0;
}

#endif
