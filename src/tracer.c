/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Tracer — STUB.
 *
 * Phase 4 of the roadmap. Real implementation will:
 *   1. Hold service_name and scope (instrumentation library name +
 *      version) for attaching to emitted spans.
 *   2. Generate random trace_id + span_id on each start_span.
 *   3. Support parent linking via start_child_span.
 *
 * ID generation should be thread-safe (one PRNG per thread, seeded
 * from a per-tracer seed) and unpredictable to callers.
 */
#include <otlp-c/tracer.h>

#include <stddef.h>
#include <stdlib.h>

struct otlp_tracer {
	char placeholder; /* TODO Phase 4 */
};

otlp_tracer_t *otlp_tracer_create(const char *service_name,
				  const char *scope_name,
				  const char *scope_version)
{
	(void)service_name;
	(void)scope_name;
	(void)scope_version;
	return NULL;
}

void otlp_tracer_free(otlp_tracer_t *tracer)
{
	(void)tracer;
}

otlp_span_t *otlp_tracer_start_span(otlp_tracer_t *tracer,
				    const char *name)
{
	(void)tracer;
	(void)name;
	return NULL;
}

otlp_span_t *otlp_tracer_start_child_span(otlp_tracer_t *tracer,
					  const char *name,
					  const otlp_span_t *parent)
{
	(void)tracer;
	(void)name;
	(void)parent;
	return NULL;
}
