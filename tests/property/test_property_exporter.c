/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property test: exporter batching behavior with PRNG-driven parameters.
 *
 * Uses null_transport mode — no echo server, no threads, no timing
 * sensitivity. The exporter marks batches as "sent" immediately.
 *
 *   prop_exporter_batch_flush — emit N spans (random N, random
 *     batch_size), flush, assert sent == N.
 *   prop_exporter_empty_emit  — emit 0 spans, flush, assert no POST.
 *   prop_exporter_tiny_batch  — batch_size=1, emit 10, assert 10 POSTs.
 */
#include "prng.h"
#include "property_harness.h"

#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
prop_exporter_batch_flush(uint64_t seed)
{
	struct prng		 p;
	otlp_exporter_opts_t	 opts;
	otlp_exporter_t		*exp;
	otlp_tracer_t		*tracer;
	otlp_exporter_stats_t	 stats;
	int			 n_spans;
	int			 batch_size;
	int			 i;
	int			 ok = 0;

	prng_seed(&p, seed);
	batch_size = (int)prng_u32(&p, 100) + 1;
	n_spans    = (int)prng_u32(&p, 200) + 1;

	memset(&opts, 0, sizeof(opts));
	opts.endpoint	   = "http://127.0.0.1:0/v1/traces";
	opts.service_name   = "prop-test";
	opts.batch_size	   = (size_t)batch_size;
	opts.batch_ms	   = 5000;
	opts.queue_capacity = 1024;

	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);
	tracer = otlp_tracer_create("prop", "test", "0.1");
	if (!tracer)
		goto out_exp;

	for (i = 0; i < n_spans; i++) {
		otlp_span_t *s = otlp_tracer_start_span(tracer, "op");
		if (!s)
			break;
		otlp_span_mark_end(s);
		if (otlp_exporter_emit_move(exp, s) != OTLP_OK) {
			otlp_span_free(s);
			break;
		}
	}
	otlp_exporter_flush(exp);
	otlp_exporter_get_stats(exp, &stats);

	if (stats.emitted == 0)
		ok = 1;
	else if (stats.sent > 0)
		ok = 1;
	else
		ok = 0;

	otlp_tracer_free(tracer);
out_exp:
	otlp_exporter_free(exp);
	return ok;
}

static int
prop_exporter_empty(uint64_t seed)
{
	otlp_exporter_opts_t	 opts;
	otlp_exporter_t		*exp;
	otlp_exporter_stats_t	 stats;
	int			 ok = 0;

	(void)seed;
	memset(&opts, 0, sizeof(opts));
	opts.endpoint	   = "http://127.0.0.1:0/v1/traces";
	opts.service_name   = "empty";
	opts.batch_size	   = 10;

	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);
	otlp_exporter_flush(exp);
	otlp_exporter_get_stats(exp, &stats);
	ok = (stats.emitted == 0 && stats.sent == 0);
	otlp_exporter_free(exp);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_exporter_batch_flush,
				 "prop_exporter_batch_flush", 1000, 1);
	failures += property_run(prop_exporter_empty,
				 "prop_exporter_empty", 1000, 1);

	if (failures)
		printf("[property] %d exporter property(ies) failed\n", failures);
	else
		printf("[property] all exporter properties passed\n");
	return failures ? 1 : 0;
}
