/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Integration test: emit 100 spans to a local otelcol + Jaeger
 * deployment, query Jaeger's API, assert the spans appear.
 *
 * Skipped unless OTLP_C_RUN_INTEGRATION=1 is set in the env, so
 * missing Docker doesn't fail local dev.
 *
 * Requires:
 *   cd tests/integration && docker compose up -d && cd -
 *   OTLP_C_RUN_INTEGRATION=1 ctest --test-dir build -L integration
 */
#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>
#include <otlp-c/version.h>

#include "../src/http_client.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Tiny blocking HTTP client for the Jaeger query (test-only). The
 * main library is non-blocking + caller-tick; the test wants a
 * one-shot sync call. We re-use otlp_http_request_t and busy-loop. */
static otlp_status_t
blocking_get(const char *url_str,
	uint8_t **body_out,
	size_t *len_out,
	int *status_out)
{
	struct otlp_http_url url;
	otlp_http_request_t *req = NULL;
	otlp_status_t st;

	st = otlp_http_parse_url(url_str, &url);
	if (st != OTLP_OK)
		return st;
	st = otlp_http_request_start(&req, &url, "otlp-c/test", NULL, 0, 0, 0);
	if (st != OTLP_OK)
		return st;
	for (int i = 0; i < 100000; i++)
	{
		otlp_http_req_state_t s;

		(void) otlp_http_request_step(req);
		s = otlp_http_request_state(req);
		if (s == OTLP_HTTP_REQ_DONE)
		{
			const uint8_t *p;
			size_t n;

			p = otlp_http_request_body(req, &n);
			*status_out = otlp_http_request_http_status(req);
			if (n > 0)
			{
				*body_out = malloc(n);
				if (!*body_out)
				{
					otlp_http_request_free(req);
					return OTLP_ERR_NOMEM;
				}
				memcpy(*body_out, p, n);
				*len_out = n;
			}
			else
			{
				*body_out = NULL;
				*len_out = 0;
			}
			otlp_http_request_free(req);
			return OTLP_OK;
		}
		if (s == OTLP_HTTP_REQ_FAILED)
		{
			otlp_http_request_free(req);
			return OTLP_ERR_NETWORK;
		}
		struct timespec ts = { 0, 1000 * 1000 /* 1ms */ };
		nanosleep(&ts, NULL);
	}
	otlp_http_request_free(req);
	return OTLP_ERR_TIMEOUT;
}

static char *
gen_test_run_id(void)
{
	static char buf[33];

	snprintf(buf,
		sizeof(buf),
		"%016llx",
		(unsigned long long) time(NULL) * 1000000ULL);
	return buf;
}

int
main(void)
{
	const char *enabled;
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	otlp_status_t st;
	char *run_id;
	int i;

	enabled = getenv("OTLP_C_RUN_INTEGRATION");
	if (!enabled || enabled[0] == '\0')
	{
		printf("[integration] skipped (set OTLP_C_RUN_INTEGRATION=1 to "
		       "run)\n");
		return 0;
	}

	run_id = gen_test_run_id();
	printf("[integration] test_run_id=%s\n", run_id);

	memset(&opts, 0, sizeof(opts));
	opts.endpoint = "http://localhost:4318/v1/traces";
	opts.service_name = "otlp-c-integration-test";
	opts.batch_size = 25;
	opts.batch_ms = 200;
	exp = otlp_exporter_create(&opts);
	assert(exp != NULL);

	tracer = otlp_tracer_create("otlp-c-integration-test",
		"integration",
		OTLP_C_VERSION_STRING);
	assert(tracer != NULL);

	for (i = 0; i < 100; i++)
	{
		otlp_span_t *s = otlp_tracer_start_span(tracer, "op");
		assert(s != NULL);
		otlp_span_set_attribute_string(s, "test_run_id", run_id);
		otlp_span_set_attribute_int(s, "i", (int64_t) i);
		st = otlp_exporter_emit(exp, s);
		assert(st == OTLP_OK);
		otlp_span_free(s);
	}

	st = otlp_exporter_flush(exp);
	assert(st == OTLP_OK);
	otlp_exporter_shutdown(exp);
	otlp_exporter_free(exp);
	otlp_tracer_free(tracer);

	/* Poll Jaeger for the service + tag for up to 10s. */
	for (int attempt = 0; attempt < 100; attempt++)
	{
		uint8_t *body = NULL;
		size_t len = 0;
		int http_status = 0;
		char url[256];

		snprintf(url,
			sizeof(url),
			"http://localhost:16686/api/traces"
			"?service=otlp-c-integration-test&limit=200");

		st = blocking_get(url, &body, &len, &http_status);
		if (st == OTLP_OK && http_status == 200 && body)
		{
			/* Look for our test_run_id in the response
			 * (memmem is glibc-only; hand-roll a tiny
			 * substring search to keep Windows clean). */
			size_t       hay = 0;
			const size_t nlen = strlen(run_id);
			bool	     found = false;

			if (nlen == 0 || len < nlen)
				found = false;
			else
				for (hay = 0; hay + nlen <= len; hay++) {
					if (memcmp(body + hay,
						   run_id, nlen) == 0) {
						found = true;
						break;
					}
				}
			if (found)
			{
				printf("[integration] PASS — span with "
				       "test_run_id=%s visible in Jaeger\n",
					run_id);
				free(body);
				return 0;
			}
			free(body);
		}
		struct timespec ts = { 0, 100 * 1000 * 1000 /* 100ms */ };
		nanosleep(&ts, NULL);
	}
	printf("[integration] FAIL — span not visible in Jaeger after 10s\n");
	return 1;
}
