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
#include "../src/platform.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Tiny blocking raw-socket HTTP GET for the Jaeger query (test-only).
 *
 * Why not otlp_http_request_t: the library's client is POST-only
 * (OTLP only ever POSTs). Jaeger's query API rejects POST with 405
 * Method Not Allowed. This helper hand-builds a GET over the
 * platform socket layer and blocks until EOF (Connection: close). */
static otlp_status_t
blocking_get(const char *url_str,
	uint8_t **body_out,
	size_t *len_out,
	int *status_out)
{
	struct otlp_http_url url;
	otlp_socket_t *sock = NULL;
	otlp_status_t st;
	char req[512];
	size_t req_len;
	size_t cap = 1024 * 1024;
	size_t len = 0;
	uint8_t *buf;
	int i;

	st = otlp_http_parse_url(url_str, &url);
	if (st != OTLP_OK)
		return st;

	req_len = (size_t) snprintf(req, sizeof(req),
		"GET %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"User-Agent: otlp-c/test\r\n"
		"Connection: close\r\n"
		"\r\n",
		url.path, url.host);
	if (req_len == 0 || req_len >= sizeof(req))
		return OTLP_ERR_OVERFLOW;

	st = otlp_socket_connect(&sock, url.host, url.port);
	if (st != OTLP_OK)
		return st;

	buf = malloc(cap);
	if (!buf)
	{
		otlp_socket_close(sock);
		return OTLP_ERR_NOMEM;
	}

	/* Blocking write loop (test-only; the socket is non-blocking
	 * so we spin with a short sleep until fully written). */
	{
		size_t sent = 0;

		for (i = 0; i < 100000 && sent < req_len; i++)
		{
			size_t n = 0;

			st = otlp_socket_write(sock,
				(const uint8_t *) req + sent,
				req_len - sent, &n);
			if (st == OTLP_OK)
				sent += n;
			else if (st != OTLP_ERR_WOULDBLOCK)
			{
				free(buf);
				otlp_socket_close(sock);
				return st;
			}
			if (sent < req_len)
			{
				struct timespec ts = { 0, 1000 * 1000 };
				nanosleep(&ts, NULL);
			}
		}
		if (sent < req_len)
		{
			free(buf);
			otlp_socket_close(sock);
			return OTLP_ERR_TIMEOUT;
		}
	}

	/* Blocking read loop until EOF (Connection: close). */
	for (i = 0; i < 100000; i++)
	{
		size_t n = 0;

		st = otlp_socket_read(sock, buf + len, cap - len, &n);
		if (st == OTLP_OK)
		{
			if (n == 0)
				break; /* EOF */
			len += n;
			if (len == cap)
			{
				/* Response too large; cap it. */
				break;
			}
		}
		else if (st == OTLP_ERR_WOULDBLOCK)
		{
			struct timespec ts = { 0, 1000 * 1000 };
			nanosleep(&ts, NULL);
		}
		else
			break; /* error */
	}
	otlp_socket_close(sock);

	/* Parse status line: "HTTP/1.1 NNN ..." */
	if (len < 12 || memcmp(buf, "HTTP/", 5) != 0)
	{
		free(buf);
		return OTLP_ERR_INVALID_RESPONSE;
	}
	{
		const uint8_t *p = buf + 5;

		while (p < buf + len && *p != ' ')
			p++;
		p++;
		if (p + 3 > buf + len ||
		    p[0] < '0' || p[0] > '9' ||
		    p[1] < '0' || p[1] > '9' ||
		    p[2] < '0' || p[2] > '9')
		{
			free(buf);
			return OTLP_ERR_INVALID_RESPONSE;
		}
		*status_out = (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');
	}

	/* Find body (after \r\n\r\n) and return a copy. The needles
	 * the caller searches for never appear in headers, so a
	 * simple split is sufficient. */
	{
		uint8_t *hdr_end = NULL;

		for (size_t k = 0; k + 3 < len; k++)
		{
			if (buf[k] == '\r' && buf[k + 1] == '\n' &&
			    buf[k + 2] == '\r' && buf[k + 3] == '\n')
			{
				hdr_end = buf + k + 4;
				break;
			}
		}
		if (!hdr_end)
		{
			free(buf);
			return OTLP_ERR_INVALID_RESPONSE;
		}
		*len_out = (size_t) (buf + len - hdr_end);
		if (*len_out > 0)
		{
			*body_out = malloc(*len_out);
			if (!*body_out)
			{
				free(buf);
				return OTLP_ERR_NOMEM;
			}
			memcpy(*body_out, hdr_end, *len_out);
		}
		else
			*body_out = NULL;
	}
	free(buf);
	return OTLP_OK;
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
		/* Exercise the v0.5.48 fixes end-to-end: events (field
		 * numbers were swapped pre-fix) and status (code was at
		 * the wrong field pre-fix). If the collector rejects or
		 * drops these fields, the span won't appear complete in
		 * Jaeger and the substring assertions below will fail. */
		otlp_span_add_event(s, "cache-miss", 0);
		otlp_span_set_event_attribute_string(s, "key", "user_42");
		otlp_span_set_status(s, OTLP_STATUS_CODE_OK, NULL);
		st = otlp_exporter_emit(exp, s);
		assert(st == OTLP_OK);
		otlp_span_free(s);
	}

	st = otlp_exporter_flush(exp);
	assert(st == OTLP_OK);
	{
		otlp_exporter_stats_t stats;

		otlp_exporter_get_stats(exp, &stats);
		printf("[integration] stats: emitted=%llu sent=%llu "
		       "dropped_err=%llu dropped_full=%llu "
		       "http_2xx=%llu http_4xx=%llu http_5xx=%llu "
		       "network_err=%llu\n",
		       (unsigned long long) stats.emitted,
		       (unsigned long long) stats.sent,
		       (unsigned long long) stats.dropped_err,
		       (unsigned long long) stats.dropped_full,
		       (unsigned long long) stats.http_2xx,
		       (unsigned long long) stats.http_4xx,
		       (unsigned long long) stats.http_5xx,
		       (unsigned long long) stats.network_err);
	}
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
		if (attempt == 0 || attempt == 50)
			printf("[integration] poll %d: st=%d http=%d len=%zu"
			       " body[:120]=%.*s\n",
			       attempt, (int) st, http_status, len,
			       (int) (len < 120 ? len : 120),
			       body ? (const char *) body : "(null)");
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
				/* v0.5.48 end-to-end validation: verify the
				 * event name and status survive the round-trip
				 * (encode → otelcol decode → Jaeger store →
				 * query). Events appear as span logs; status
				 * appears as the "otel.status_code" tag. */
				bool found_event = false;
				bool found_status = false;
				const char *needle;

				needle = "cache-miss";
				for (hay = 0;
				     hay + strlen(needle) <= len; hay++) {
					if (memcmp(body + hay, needle,
						   strlen(needle)) == 0) {
						found_event = true;
						break;
					}
				}
				/* Status: search for the "otel.status_code"
				 * tag key (otelcol's translation of OTLP
				 * Status). The exact value serialization
				 * varies across otelcol/Jaeger versions
				 * ("STATUS_CODE_OK" / "Ok" / "ok"), so the
				 * key is the robust needle — its presence
				 * proves the status sub-message survived
				 * the round-trip. */
				needle = "status_code";
				for (hay = 0;
				     hay + strlen(needle) <= len; hay++) {
					if (memcmp(body + hay, needle,
						   strlen(needle)) == 0) {
						found_status = true;
						break;
					}
				}
				free(body);
				if (!found_event)
				{
					printf("[integration] FAIL — spans "
					       "visible but event 'cache-miss' "
					       "missing (v0.5.48 Event fix not "
					       "validated)\n");
					return 1;
				}
				if (!found_status)
				{
					printf("[integration] FAIL — spans "
					       "visible but status_code tag "
					       "missing (v0.5.48 Status fix "
					       "not validated)\n");
					return 1;
				}
				printf("[integration] PASS — span with "
				       "test_run_id=%s + event + status "
				       "visible in Jaeger\n",
					run_id);
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
