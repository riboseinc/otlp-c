/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Sampler implementations. See include/otlp-c/sampler.h.
 *
 * The built-in samplers are stateless after construction — the
 * should_sample() function reads only its arguments and the
 * (immutable) ratio / decision configuration. This makes them
 * safe to call from multiple threads without synchronization.
 */
#include <otlp-c/sampler.h>
#include <otlp-c/span.h>

#include "internal_util.h"

#include <stdint.h>
#include <string.h>

/* ── AlwaysOn ─────────────────────────────────────────────────── */

static otlp_sampling_result_t
always_on_should_sample(const otlp_sampler_t *sampler,
			const uint8_t		trace_id[16],
			const char	       *name,
			otlp_span_kind_t	kind)
{
	otlp_sampling_result_t r;

	(void) sampler;
	(void) trace_id;
	(void) name;
	(void) kind;
	r.decision = OTLP_SAMPLING_DECISION_RECORD_AND_SAMPLED;
	return r;
}

otlp_sampler_t *
otlp_sampler_always_on(void)
{
	static otlp_sampler_t static_always_on = {
		.should_sample = always_on_should_sample,
		.free = NULL,
	};
	return &static_always_on;
}

/* ── AlwaysOff ────────────────────────────────────────────────── */

static otlp_sampling_result_t
always_off_should_sample(const otlp_sampler_t *sampler,
			 const uint8_t	    trace_id[16],
			 const char		      *name,
			 otlp_span_kind_t	     kind)
{
	otlp_sampling_result_t r;

	(void) sampler;
	(void) trace_id;
	(void) name;
	(void) kind;
	r.decision = OTLP_SAMPLING_DECISION_NOT_RECORD;
	return r;
}

otlp_sampler_t *
otlp_sampler_always_off(void)
{
	static otlp_sampler_t static_always_off = {
		.should_sample = always_off_should_sample,
		.free = NULL,
	};
	return &static_always_off;
}

/* ── TraceIdRatioBased ────────────────────────────────────────── */

struct ratio_sampler {
	otlp_sampler_t base;
	double	       ratio;
};

static otlp_sampling_result_t
ratio_should_sample(const otlp_sampler_t *sampler,
		    const uint8_t	      trace_id[16],
		    const char		     *name,
		    otlp_span_kind_t	      kind)
{
	const struct ratio_sampler *rs = (const struct ratio_sampler *) sampler;
	otlp_sampling_result_t	r;
	uint64_t		trace_prefix;

	(void) name;
	(void) kind;
	/* Use the first 8 bytes of trace_id as a deterministic
	 * threshold input. Same trace_id → same decision. */
	memcpy(&trace_prefix, trace_id, 8);
	/* threshold = ratio * UINT64_MAX (approximate). Compare
	 * trace_prefix against it. */
	{
		double scaled = (double) trace_prefix / (double) UINT64_MAX;

		if (scaled < rs->ratio)
			r.decision = OTLP_SAMPLING_DECISION_RECORD_AND_SAMPLED;
		else
			r.decision = OTLP_SAMPLING_DECISION_NOT_RECORD;
	}
	return r;
}

static void
ratio_free(otlp_sampler_t *sampler)
{
	otlp_free(sampler);
}

otlp_sampler_t *
otlp_sampler_trace_id_ratio_based(double ratio)
{
	struct ratio_sampler *rs;

	if (ratio < 0.0)
		ratio = 0.0;
	if (ratio > 1.0)
		ratio = 1.0;
	rs = otlp_malloc(sizeof(*rs));
	if (!rs)
		return NULL;
	rs->base.should_sample = ratio_should_sample;
	rs->base.free	      = ratio_free;
	rs->ratio	      = ratio;
	return &rs->base;
}

void
otlp_sampler_free(otlp_sampler_t *sampler)
{
	if (!sampler)
		return;
	if (sampler->free)
		sampler->free(sampler);
}
