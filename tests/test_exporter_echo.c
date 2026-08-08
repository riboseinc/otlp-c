/* SPDX-License-Identifier: Apache-2.0 */
/*
 * End-to-end exporter test. Emits spans via the public API, drives
 * tick(), and verifies the echo server received them.
 */
#include "test_helper_echo.h"

#include <otlp-c/exporter.h>
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
	(void) echo_server_join(&srv, 1 * 1000 * 1000);
	printf("[exporter-echo] PASS — 10 spans emitted, %llu sent in %llu "
	       "POSTs\n",
		(unsigned long long) stats.sent,
		(unsigned long long) stats.http_2xx);
	return 0;
}

#endif
