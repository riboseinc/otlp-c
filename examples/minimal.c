/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Minimal usage example. Phase 0: prints the version and confirms
 * the library links. Phase 5+ (when the exporter is real): emits
 * one span to a local otelcol.
 *
 * Run:
 *   cmake -B build -DOTLP_C_BUILD_EXAMPLES=ON
 *   cmake --build build
 *   ./build/examples/otlp_example_minimal
 */
#include <otlp-c/otlp.h>

#include <stdio.h>

int main(void)
{
	otlp_exporter_opts_t opts = { 0 };

	opts.endpoint = "http://localhost:4318/v1/traces";
	opts.service_name = "minimal-example";

	printf("otlp-c %s\n", otlp_version());
	printf("Phase 0 stub: the exporter API exists but returns NULL.\n");
	printf("Once Phase 5 lands, this example will emit a real span.\n");

	otlp_exporter_t *exp = otlp_exporter_create(&opts);

	if (!exp) {
		printf("(as expected: exporter_create returned NULL — stub)\n");
		return 0;
	}

	/* Once the exporter is real:
	 *
	 *   otlp_tracer_t *tracer = otlp_tracer_create("my-service",
	 *                                              "minimal-example",
	 *                                              "0.1.0");
	 *   otlp_span_t *span = otlp_tracer_start_span(tracer, "do-work");
	 *   otlp_span_set_attribute_string(span, "user.id", "alice");
	 *   otlp_span_mark_end(span);
	 *   otlp_exporter_emit(exp, span);
	 *   otlp_exporter_flush(exp);
	 *   otlp_span_free(span);
	 *   otlp_tracer_free(tracer);
	 *   otlp_exporter_free(exp);
	 */
	otlp_exporter_free(exp);
	return 0;
}
