/* SPDX-License-Identifier: Apache-2.0 */
/*
 * End-to-end exporter test. Emits spans via the public API, drives
 * tick(), and verifies the echo server received them.
 */
#include "test_helper_echo.h"

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
	assert(st == OTLP_OK);

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
	assert(exp != NULL);

	tracer = otlp_tracer_create("svc", "test", "1.0");
	assert(tracer != NULL);

	/* Emit 10 spans — should produce ~3 batches (4+4+2). */
	for (int i = 0; i < 10; i++)
	{
		otlp_span_t *s = otlp_tracer_start_span(tracer, "op");
		assert(s != NULL);
		st = otlp_exporter_emit(exp, s);
		assert(st == OTLP_OK);
		otlp_span_free(s);
	}

	/* Drive tick until flushed. */
	st = otlp_exporter_flush(exp);
	assert(st == OTLP_OK);

	otlp_exporter_get_stats(exp, &stats);
	printf("[exporter-echo] emitted=%llu sent=%llu 2xx=%llu\n",
		(unsigned long long) stats.emitted,
		(unsigned long long) stats.sent,
		(unsigned long long) stats.http_2xx);
	assert(stats.emitted == 10);
	assert(stats.sent == 10);
	assert(stats.http_2xx >= 2); /* at least 2 batches */

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
		assert(exp != NULL);

		{
			otlp_metric_t *m =
				otlp_metric_create(OTLP_METRIC_COUNTER,
					"http_requests_total",
					"1",
					"requests",
					NULL,
					0);

			assert(m != NULL);
			assert(otlp_metric_record(m, 5.0) == OTLP_OK);
			otlp_metric_mark_time(m);
			otlp_metric_set_attribute_string(m, "method", "GET");
			st = otlp_exporter_emit_metric_move(exp, m);
			assert(st == OTLP_OK);
		}
		st = otlp_exporter_flush(exp);
		assert(st == OTLP_OK);
		otlp_exporter_get_stats(exp, &stats);
		printf("[exporter-echo] metrics: emitted=%llu sent=%llu\n",
			(unsigned long long) stats.emitted_metrics,
			(unsigned long long) stats.sent_metrics);
		assert(stats.emitted_metrics == 1);
		assert(stats.sent_metrics == 1);
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
		assert(exp != NULL);
		{
			otlp_log_record_t *lr = otlp_log_record_create(
				OTLP_SEVERITY_INFO, "coverage test log");
			otlp_log_record_t *arr[2];

			assert(lr != NULL);
			otlp_log_record_mark_timestamp(lr);
			otlp_log_record_set_attribute_string(lr, "k", "v");
			/* second record exercises batching */
			arr[0] = lr;
			(void) arr;
			st = otlp_exporter_emit_log_move(exp, lr);
			assert(st == OTLP_OK);
			{
				otlp_log_record_t *lr2 = otlp_log_record_create(
					OTLP_SEVERITY_WARN, "second");
				assert(lr2 != NULL);
				st = otlp_exporter_emit_log_move(exp, lr2);
				assert(st == OTLP_OK);
			}
		}
		st = otlp_exporter_flush(exp);
		assert(st == OTLP_OK);
		otlp_exporter_get_stats(exp, &stats);
		printf("[exporter-echo] logs: emitted=%llu sent=%llu\n",
			(unsigned long long) stats.emitted_logs,
			(unsigned long long) stats.sent_logs);
		assert(stats.emitted_logs == 2);
		assert(stats.sent_logs == 2);
		otlp_exporter_free(exp);
	}

	(void) echo_server_join(&srv, 1 * 1000 * 1000);
	printf("[exporter-echo] PASS — spans + metrics + logs over HTTP\n");
	return 0;
}

#endif
