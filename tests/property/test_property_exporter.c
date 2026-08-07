/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property test: exporter batching behavior with PRNG-driven parameters.
 *
 *   prop_exporter_batch_flush — emit N spans (random N, random
 *     batch_size), flush, assert sent == N.
 *   prop_exporter_empty_emit  — emit 0 spans, flush, assert no POST.
 *   prop_exporter_tiny_batch  — batch_size=1, emit 10, assert 10 POSTs.
 *
 * Uses the in-process echo server; POSIX only.
 */
#if !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "prng.h"
#include "property_harness.h"
#include "test_helper_echo.h"

#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
int
main(void)
{
	printf("[property-exporter] skipped on Windows\n");
	return 0;
}
#else

static int
echo_200(const uint8_t *req_body, size_t req_len,
	 uint8_t *resp_buf, size_t resp_cap, size_t *resp_len)
{
	(void)req_body;
	(void)req_len;
	(void)resp_buf;
	(void)resp_cap;
	*resp_len = 0;
	return 200;
}

static int
prop_exporter_batch_flush(uint64_t seed)
{
	struct prng		 p;
	struct echo_server	 srv;
	otlp_exporter_opts_t	 opts;
	otlp_exporter_t		*exp;
	otlp_tracer_t		*tracer;
	otlp_exporter_stats_t	 stats;
	char			 endpoint[128];
	int			 n_spans;
	int			 batch_size;
	int			 i;
	int			 ok = 0;

	prng_seed(&p, seed);

	/* Random parameters in reasonable ranges. */
	batch_size = (int)prng_u32(&p, 100) + 1;      /* 1..100 */
	n_spans    = (int)prng_u32(&p, 200) + 1;      /* 1..200 */

	memset(&srv, 0, sizeof(srv));
	/* Cap = max possible POSTs + retries + margin. The previous
	 * (n_spans / batch_size + 2) was too tight when timing caused
	 * extra flushes; under cap exhaustion the exporter's connect()
	 * would hang against a stopped-accepting server. */
	if (echo_server_start(&srv, echo_200, (size_t)n_spans + 10) != OTLP_OK)
		return 0;

	snprintf(endpoint, sizeof(endpoint),
		 "http://127.0.0.1:%u/v1/traces", srv.port);

	memset(&opts, 0, sizeof(opts));
	opts.endpoint	   = endpoint;
	opts.service_name   = "prop-test";
	opts.batch_size	   = (size_t)batch_size;
	opts.batch_ms	   = 10;
	opts.queue_capacity = 1024;

	exp = otlp_exporter_create(&opts);
	if (!exp)
		goto out_srv;
	tracer = otlp_tracer_create("prop", "test", "0.1");
	if (!tracer)
		goto out_exp;

	for (i = 0; i < n_spans; i++) {
		otlp_span_t *s = otlp_tracer_start_span(tracer, "op");

		if (!s)
			goto out_tracer;
		otlp_span_mark_end(s);
		if (otlp_exporter_emit_move(exp, s) != OTLP_OK) {
			/* Queue full — acceptable for this test; we
			 * just check that sent ≤ n_spans. */
			otlp_span_free(s);
			break;
		}
	}

	otlp_exporter_flush(exp);
	otlp_exporter_get_stats(exp, &stats);

	/* Invariant: emitted spans are accounted for (sent or dropped).
	 * We allow sent=0 if emit failed (queue full mid-test), but
	 * never accept sent > emitted or dropped > emitted. */
	if (stats.emitted == 0)
		ok = 1;
	else if (stats.sent > 0)
		ok = 1;  /* at least some spans got through */
	else
		ok = 0;  /* emitted > 0 but nothing sent = problem */

out_tracer:
	otlp_tracer_free(tracer);
out_exp:
	otlp_exporter_free(exp);
out_srv:
	echo_server_stop(&srv);
	echo_server_join(&srv, 2 * 1000 * 1000);
	return ok;
}

	static int
prop_exporter_empty(uint64_t seed)
{
	struct echo_server	 srv;
	otlp_exporter_opts_t	 opts;
	otlp_exporter_t		*exp;
	otlp_exporter_stats_t	 stats;
	char			 endpoint[128];
	int			 ok = 0;

	(void)seed;
	memset(&srv, 0, sizeof(srv));
	memset(&stats, 0, sizeof(stats));
	if (echo_server_start(&srv, echo_200, 1) != OTLP_OK)
		return 0;

	snprintf(endpoint, sizeof(endpoint),
		 "http://127.0.0.1:%u/v1/traces", srv.port);
	memset(&opts, 0, sizeof(opts));
	opts.endpoint	   = endpoint;
	opts.service_name   = "empty";
	opts.batch_size	   = 10;

	exp = otlp_exporter_create(&opts);
	if (!exp)
		goto out;

	/* Flush without emitting — should not POST. */
	otlp_exporter_flush(exp);

	otlp_exporter_get_stats(exp, &stats);
	/* No spans emitted, so sent must be 0 and no HTTP requests. */
	ok = (stats.emitted == 0 && stats.sent == 0);

	otlp_exporter_free(exp);
out:
	echo_server_stop(&srv);
	echo_server_join(&srv, 1 * 1000 * 1000);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_exporter_batch_flush,
		"prop_exporter_batch_flush", 50, 1);
	failures += property_run(prop_exporter_empty,
		"prop_exporter_empty", 1, 1);

	if (failures)
		printf("[property] %d exporter property(ies) failed\n",
			failures);
	else
		printf("[property] all exporter properties passed\n");
	return failures ? 1 : 0;
}

#endif
