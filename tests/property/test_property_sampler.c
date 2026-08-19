/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property tests for the sampler interface.
 *
 *   prop_always_on_always_samples      — every span is sampled.
 *   prop_always_off_never_samples      — every span is dropped (NULL).
 *   prop_ratio_zero_drops_all          — ratio 0 drops everything.
 *   prop_ratio_one_keeps_all           — ratio 1 keeps everything.
 *   prop_ratio_deterministic           — same trace_id → same decision.
 *   prop_ratio_distribution            — ratio r ≈ r × N kept.
 *   prop_default_sampler_is_always_on  — start_span returns sampled span.
 */
#include "prng.h"
#include "property_harness.h"

#include <otlp-c/sampler.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static int
prop_always_on_always_samples(uint64_t seed)
{
	otlp_sampler_t *s = otlp_sampler_always_on();
	struct prng p;
	uint8_t trace_id[16];

	prng_seed(&p, seed);
	for (size_t i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(&p, 256);
	for (size_t i = 0; i < 100; i++)
	{
		otlp_sampling_result_t r = s->should_sample(
			s, trace_id, "x", OTLP_SPAN_KIND_INTERNAL);
		if (r.decision != OTLP_SAMPLING_DECISION_RECORD_AND_SAMPLED)
			return 0;
	}
	return 1;
}

static int
prop_always_off_never_samples(uint64_t seed)
{
	otlp_sampler_t *s = otlp_sampler_always_off();
	struct prng p;
	uint8_t trace_id[16];

	prng_seed(&p, seed);
	for (size_t i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(&p, 256);
	for (size_t i = 0; i < 100; i++)
	{
		otlp_sampling_result_t r = s->should_sample(
			s, trace_id, "x", OTLP_SPAN_KIND_INTERNAL);
		if (r.decision != OTLP_SAMPLING_DECISION_NOT_RECORD)
			return 0;
	}
	return 1;
}

static int
prop_ratio_zero_drops_all(uint64_t seed)
{
	otlp_sampler_t *s = otlp_sampler_trace_id_ratio_based(0.0);
	struct prng p;
	uint8_t trace_id[16];

	prng_seed(&p, seed);
	for (size_t i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(&p, 256);
	for (size_t i = 0; i < 100; i++)
	{
		otlp_sampling_result_t r = s->should_sample(
			s, trace_id, "x", OTLP_SPAN_KIND_INTERNAL);
		if (r.decision != OTLP_SAMPLING_DECISION_NOT_RECORD)
		{
			otlp_sampler_free(s);
			return 0;
		}
	}
	otlp_sampler_free(s);
	return 1;
}

static int
prop_ratio_one_keeps_all(uint64_t seed)
{
	otlp_sampler_t *s = otlp_sampler_trace_id_ratio_based(1.0);
	struct prng p;
	uint8_t trace_id[16];

	prng_seed(&p, seed);
	for (size_t i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(&p, 256);
	for (size_t i = 0; i < 100; i++)
	{
		otlp_sampling_result_t r = s->should_sample(
			s, trace_id, "x", OTLP_SPAN_KIND_INTERNAL);
		if (r.decision != OTLP_SAMPLING_DECISION_RECORD_AND_SAMPLED)
		{
			otlp_sampler_free(s);
			return 0;
		}
	}
	otlp_sampler_free(s);
	return 1;
}

static int
prop_ratio_deterministic(uint64_t seed)
{
	otlp_sampler_t *s = otlp_sampler_trace_id_ratio_based(0.5);
	struct prng p;
	uint8_t trace_id[16];

	prng_seed(&p, seed);
	for (size_t i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(&p, 256);
	{
		otlp_sampling_result_t r1 = s->should_sample(
			s, trace_id, "x", OTLP_SPAN_KIND_INTERNAL);
		otlp_sampling_result_t r2 = s->should_sample(
			s, trace_id, "x", OTLP_SPAN_KIND_INTERNAL);
		otlp_sampler_free(s);
		return r1.decision == r2.decision;
	}
}

static int
prop_ratio_distribution(uint64_t seed)
{
	/* ratio=0.5 should sample approximately half of trace_ids.
	 * We sample 1000 random trace_ids and check the kept count is
	 * within [350, 650] (loose bound; statistically very unlikely
	 * to fail). */
	otlp_sampler_t *s = otlp_sampler_trace_id_ratio_based(0.5);
	struct prng p;
	size_t kept = 0;

	prng_seed(&p, seed);
	for (size_t i = 0; i < 1000; i++)
	{
		uint8_t trace_id[16];

		for (size_t j = 0; j < 16; j++)
			trace_id[j] = (uint8_t) prng_u32(&p, 256);
		{
			otlp_sampling_result_t r = s->should_sample(
				s, trace_id, "x", OTLP_SPAN_KIND_INTERNAL);
			if (r.decision ==
				OTLP_SAMPLING_DECISION_RECORD_AND_SAMPLED)
				kept++;
		}
	}
	otlp_sampler_free(s);
	return kept >= 350 && kept <= 650;
}

static int
prop_default_sampler_is_always_on(uint64_t seed)
{
	otlp_tracer_t *tr = otlp_tracer_create("svc", NULL, NULL);
	otlp_span_t *span;

	(void) seed;
	if (!tr)
		return 0;
	span = otlp_tracer_start_span(tr, "op");
	if (!span)
	{
		otlp_tracer_free(tr);
		return 0;
	}
	{
		bool sampled = otlp_span_is_sampled(span);
		otlp_span_free(span);
		otlp_tracer_free(tr);
		return sampled;
	}
}

/* Regression (v0.5.51): ratio = 1.0 must sample EVERY trace_id,
 * including the all-0xFF trace_id (which has trace_prefix ==
 * UINT64_MAX). The pre-v0.5.51 double-comparison code computed
 * scaled = UINT64_MAX / UINT64_MAX = 1.0, then `scaled < 1.0`
 * was false → NOT sampled. The endpoint short-circuit makes the
 * boundary exact. */
static int
prop_ratio_one_samples_max_trace_id(uint64_t seed)
{
	otlp_sampler_t *s = otlp_sampler_trace_id_ratio_based(1.0);
	uint8_t trace_id[16];
	otlp_sampling_result_t r;

	(void) seed;
	memset(trace_id, 0xFF, 16);
	r = s->should_sample(s, trace_id, "x", OTLP_SPAN_KIND_INTERNAL);
	otlp_sampler_free(s);
	return r.decision == OTLP_SAMPLING_DECISION_RECORD_AND_SAMPLED;
}

/* Regression (v0.5.51): ratio = 0.0 must NOT sample, including
 * the all-zero trace_id (which has trace_prefix == 0). The
 * endpoint short-circuit makes the boundary exact. */
static int
prop_ratio_zero_drops_zero_trace_id(uint64_t seed)
{
	otlp_sampler_t *s = otlp_sampler_trace_id_ratio_based(0.0);
	uint8_t trace_id[16];
	otlp_sampling_result_t r;

	(void) seed;
	memset(trace_id, 0x00, 16);
	r = s->should_sample(s, trace_id, "x", OTLP_SPAN_KIND_INTERNAL);
	otlp_sampler_free(s);
	return r.decision == OTLP_SAMPLING_DECISION_NOT_RECORD;
}

/* The ratio threshold reads the trace-id prefix BIG-ENDIAN
 * (matching otel-go), so the decision is identical on every
 * platform: an ID starting 0x80.. is above the ratio-0.5
 * threshold (not sampled), an ID starting 0x7F.. is below it
 * (sampled). */
static int
prop_ratio_endian_known_answer(uint64_t seed)
{
	otlp_sampler_t *s = otlp_sampler_trace_id_ratio_based(0.5);
	uint8_t hi[16];
	uint8_t lo[16];
	int ok = 0;

	(void) seed;
	if (!s)
		return 0;
	memset(hi, 0, sizeof(hi));
	hi[0] = 0x80; /* BE prefix = 0x8000... > 0x7FFF... threshold */
	memset(lo, 0, sizeof(lo));
	lo[0] = 0x7F; /* BE prefix = 0x7F00... < threshold */
	if (s->should_sample(s, hi, "op", OTLP_SPAN_KIND_INTERNAL).decision ==
			OTLP_SAMPLING_DECISION_NOT_RECORD &&
		s->should_sample(s, lo, "op", OTLP_SPAN_KIND_INTERNAL)
				.decision ==
			OTLP_SAMPLING_DECISION_RECORD_AND_SAMPLED)
		ok = 1;
	otlp_sampler_free(s);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_always_on_always_samples,
		"prop_always_on_always_samples",
		5,
		1);
	failures += property_run(prop_always_off_never_samples,
		"prop_always_off_never_samples",
		5,
		1);
	failures += property_run(
		prop_ratio_zero_drops_all, "prop_ratio_zero_drops_all", 5, 1);
	failures += property_run(
		prop_ratio_one_keeps_all, "prop_ratio_one_keeps_all", 5, 1);
	failures += property_run(
		prop_ratio_deterministic, "prop_ratio_deterministic", 100, 1);
	failures += property_run(
		prop_ratio_distribution, "prop_ratio_distribution", 3, 1);
	failures += property_run(prop_default_sampler_is_always_on,
		"prop_default_sampler_is_always_on",
		1,
		1);
	failures += property_run(prop_ratio_endian_known_answer,
		"prop_ratio_endian_known_answer",
		1,
		1);
	failures += property_run(prop_ratio_one_samples_max_trace_id,
		"prop_ratio_one_samples_max_trace_id",
		1,
		1);
	failures += property_run(prop_ratio_zero_drops_zero_trace_id,
		"prop_ratio_zero_drops_zero_trace_id",
		1,
		1);

	if (failures)
		printf("[property] %d sampler property(ies) failed\n",
			failures);
	else
		printf("[property] all sampler properties passed\n");
	return failures ? 1 : 0;
}
