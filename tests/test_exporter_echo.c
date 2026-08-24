/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * End-to-end exporter test. Emits spans via the public API, drives
 * tick(), and verifies the echo server received them.
 */
#include "test_helper_echo.h"
#include "test_util.h"

#include <otlp-c/exporter.h>
#include <otlp-c/log.h>
#include <otlp-c/metric.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
int
main(void)
{
	printf("[exporter-echo] skipped on Windows\n");
	return 0;
}
#else

static int
count_handler(const uint8_t *req_body,
	size_t req_len,
	uint8_t *resp_buf,
	size_t resp_cap,
	size_t *resp_len)
{
	(void) req_body;
	(void) req_len;
	(void) resp_buf;
	(void) resp_cap;
	*resp_len = 0;
	return 200;
}

int
main(void)
{
	struct echo_server srv;
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	char endpoint[128];
	otlp_exporter_stats_t stats;
	otlp_status_t st;

	st = echo_server_start(&srv, count_handler, 16);
	check_true(st == OTLP_OK);

	snprintf(endpoint,
		sizeof(endpoint),
		"http://127.0.0.1:%u/v1/traces",
		srv.port);

	memset(&opts, 0, sizeof(opts));
	opts.endpoint = endpoint;
	opts.service_name = "test";
	opts.batch_size = 4; /* small batch to force multiple */
	opts.batch_ms = 50;
	opts.queue_capacity = 256;
	exp = otlp_exporter_create(&opts);
	check_true(exp != NULL);

	tracer = otlp_tracer_create("svc", "test", "1.0");
	check_true(tracer != NULL);

	/* Emit 10 spans — should produce ~3 batches (4+4+2). */
	for (int i = 0; i < 10; i++)
	{
		otlp_span_t *s = otlp_tracer_start_span(tracer, "op");
		check_true(s != NULL);
		st = otlp_exporter_emit(exp, s);
		check_true(st == OTLP_OK);
		otlp_span_free(s);
	}

	/* Drive tick until flushed. */
	st = otlp_exporter_flush(exp);
	check_true(st == OTLP_OK);

	otlp_exporter_get_stats(exp, &stats);
	printf("[exporter-echo] emitted=%llu sent=%llu 2xx=%llu\n",
		(unsigned long long) stats.emitted,
		(unsigned long long) stats.sent,
		(unsigned long long) stats.http_2xx);
	check_true(stats.emitted == 10);
	check_true(stats.sent == 10);
	check_true(stats.http_2xx >= 2); /* at least 2 batches */

	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);

	/* ── Case 2: async metric export through the real HTTP path
	 * (v0.5.90: exporter_otel's metric build path had zero test
	 * coverage — null_transport tests skip it entirely). */
	{
		char m_endpoint[128];

		snprintf(m_endpoint,
			sizeof(m_endpoint),
			"http://127.0.0.1:%u/v1/metrics",
			srv.port);
		opts.endpoint = m_endpoint;
		exp = otlp_exporter_create(&opts);
		check_true(exp != NULL);

		{
			otlp_metric_t *m =
				otlp_metric_create(OTLP_METRIC_COUNTER,
					"http_requests_total",
					"1",
					"requests",
					NULL,
					0);

			check_true(m != NULL);
			{
				otlp_status_t rc = otlp_metric_record(m, 5.0);

				check_true(rc == OTLP_OK);
			}
			otlp_metric_mark_time(m);
			otlp_metric_set_attribute_string(m, "method", "GET");
			st = otlp_exporter_emit_metric_move(exp, m);
			check_true(st == OTLP_OK);
		}
		st = otlp_exporter_flush(exp);
		check_true(st == OTLP_OK);
		otlp_exporter_get_stats(exp, &stats);
		printf("[exporter-echo] metrics: emitted=%llu sent=%llu\n",
			(unsigned long long) stats.emitted_metrics,
			(unsigned long long) stats.sent_metrics);
		check_true(stats.emitted_metrics == 1);
		check_true(stats.sent_metrics == 1);
		otlp_exporter_free(exp);
	}

	/* ── Case 3: async log export through the real HTTP path. */
	{
		char l_endpoint[128];

		snprintf(l_endpoint,
			sizeof(l_endpoint),
			"http://127.0.0.1:%u/v1/logs",
			srv.port);
		opts.endpoint = l_endpoint;
		exp = otlp_exporter_create(&opts);
		check_true(exp != NULL);
		{
			otlp_log_record_t *lr = otlp_log_record_create(
				OTLP_SEVERITY_INFO, "coverage test log");
			otlp_log_record_t *arr[2];

			check_true(lr != NULL);
			otlp_log_record_mark_timestamp(lr);
			otlp_log_record_set_attribute_string(lr, "k", "v");
			/* second record exercises batching */
			arr[0] = lr;
			(void) arr;
			st = otlp_exporter_emit_log_move(exp, lr);
			check_true(st == OTLP_OK);
			{
				otlp_log_record_t *lr2 = otlp_log_record_create(
					OTLP_SEVERITY_WARN, "second");
				check_true(lr2 != NULL);
				st = otlp_exporter_emit_log_move(exp, lr2);
				check_true(st == OTLP_OK);
			}
		}
		st = otlp_exporter_flush(exp);
		check_true(st == OTLP_OK);
		otlp_exporter_get_stats(exp, &stats);
		printf("[exporter-echo] logs: emitted=%llu sent=%llu\n",
			(unsigned long long) stats.emitted_logs,
			(unsigned long long) stats.sent_logs);
		check_true(stats.emitted_logs == 2);
		check_true(stats.sent_logs == 2);
		otlp_exporter_free(exp);
	}

	/* 16 offered, ~4 arrive: stop deterministically (v0.5.96
	 * lesson — a worker still in accept() outlives this frame).
	 * _stop wakes the worker (self-connect) and it exits, closing
	 * the listen fd; the join below must succeed. */
	echo_server_stop(&srv);
	check_ok(echo_server_join(&srv, 1 * 1000 * 1000));

	/* v0.7.3: per-signal endpoint override — flush_metric must
	 * POST to the METRICS signal's own URL, not the traces one. */
	{
		struct echo_server srv2;
		otlp_metric_t *m = otlp_metric_create(
			OTLP_METRIC_COUNTER, "override_probe", "1", NULL,
			NULL, 0);
		char override[192];
		const uint8_t *wire;
		size_t wire_len;

		check_true(m != NULL);
		check_ok(echo_server_start(&srv2, NULL, 1));
		snprintf(override,
			sizeof(override),
			"http://127.0.0.1:%u/custom-metrics",
			srv2.port);
		memset(&opts, 0, sizeof(opts));
		opts.endpoint = endpoint;
		opts.metrics_endpoint = override;
		opts.service_name = "test";
		exp = otlp_exporter_create(&opts);
		check_true(exp != NULL);
		otlp_metric_record(m, 1.0);
		check_ok(otlp_exporter_flush_metric(exp, m));
		otlp_metric_free(m);
		otlp_exporter_free(exp);
		echo_server_stop(&srv2);
		check_ok(echo_server_join(&srv2, 1 * 1000 * 1000));
		wire = echo_server_last_request(&wire_len);
		check_true(wire != NULL && wire_len > 0);
		check_true(memmem(wire,
				  wire_len,
				  "POST /custom-metrics HTTP/1.1",
				  strlen("POST /custom-metrics HTTP/1.1")) !=
			NULL);
	}

	printf("[exporter-echo] PASS — spans + metrics + logs over HTTP\n");
	return 0;
}

#endif
