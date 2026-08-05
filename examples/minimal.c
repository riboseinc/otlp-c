/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Minimal usage example. Builds + runs against any local otelcol
 * listening on the default OTLP/HTTP port (4318).
 *
 *   cmake -B build -DOTLP_C_BUILD_EXAMPLES=ON
 *   cmake --build build
 *   ./build/examples/otlp_example_minimal
 *
 * For the spans to land somewhere visible, run a local otelcol +
 * Jaeger via the integration test topology:
 *
 *   cd tests/integration && docker compose up -d && cd ../..
 *
 * Then visit http://localhost:16686 to see the emitted span.
 */
#include <otlp-c/otlp.h>

#include <stdio.h>

int
main(void)
{
	otlp_exporter_opts_t opts = { 0 };
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	otlp_span_t *span;

	/* Zero-initialized opts picks library defaults for endpoint
	 * (http://localhost:4318/v1/traces), batch_size (512),
	 * batch_ms (100), retry/backoff, and User-Agent. */
	opts.service_name = "minimal-example";

	exp = otlp_exporter_create(&opts);
	if (!exp)
	{
		fprintf(stderr, "otlp_exporter_create failed\n");
		return 1;
	}

	tracer = otlp_tracer_create(
		"minimal-example", "minimal", otlp_version());
	if (!tracer)
	{
		otlp_exporter_free(exp);
		return 1;
	}

	span = otlp_tracer_start_span(tracer, "do-work");
	if (!span)
	{
		otlp_tracer_free(tracer);
		otlp_exporter_free(exp);
		return 1;
	}

	otlp_span_set_attribute_string(span, "user.id", "alice");
	otlp_span_set_attribute_int(span, "attempt", 1);
	otlp_span_mark_end(span);

	otlp_exporter_emit(exp, span);
	otlp_exporter_flush(exp); /* block until drained */

	otlp_span_free(span);
	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);

	printf("otlp-c %s — minimal example emitted 1 span\n", otlp_version());
	return 0;
}
