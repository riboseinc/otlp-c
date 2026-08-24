/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Property test: traceparent format → parse round-trip.
 */
#include "prng.h"
#include "property_harness.h"

#include <otlp-c/otlp.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>
#include <otlp-c/w3c.h>

#include <stdint.h>
#include <string.h>

static int
prop_traceparent_roundtrip(uint64_t seed)
{
	struct prng p;
	otlp_tracer_t *t;
	otlp_span_t *span;
	char buf1[OTLP_TRACEPARENT_BUF_SIZE];
	char buf2[OTLP_TRACEPARENT_BUF_SIZE];
	size_t len1 = 0;
	size_t len2 = 0;
	uint8_t trace_id[16];
	uint8_t span_id[8];
	uint8_t flags = 0;
	int ok = 0;

	prng_seed(&p, seed);
	t = otlp_tracer_create("test", "test", "0.1");
	if (!t)
		return 0;
	span = otlp_tracer_start_span(t, "op");
	if (!span)
		goto out_tracer;

	bool sampled = (prng_next(&p) & 1) ? true : false;

	if (otlp_traceparent_format(span, sampled, buf1, sizeof(buf1), &len1) !=
		OTLP_OK)
		goto out;
	if (otlp_traceparent_parse(buf1, trace_id, span_id, &flags) != OTLP_OK)
		goto out;
	if (otlp_traceparent_format(span, sampled, buf2, sizeof(buf2), &len2) !=
		OTLP_OK)
		goto out;
	ok = (len1 == len2 && memcmp(buf1, buf2, len1) == 0);

out:
	otlp_span_free(span);
out_tracer:
	otlp_tracer_free(t);
	return ok;
}

static int
prop_traceparent_reject_zero(uint64_t seed)
{
	(void) seed;
	uint8_t trace_id[16] = { 0 };
	uint8_t span_id[8] = { 0 };
	uint8_t flags = 0;

	if (otlp_traceparent_parse("00-00000000000000000000000000000000-"
				   "0000000000000000-00",
		    trace_id,
		    span_id,
		    &flags) == OTLP_OK)
		return 0;
	return 1;
}

static int
prop_traceparent_reject_malformed(uint64_t seed)
{
	(void) seed;
	uint8_t trace_id[16];
	uint8_t span_id[8];
	uint8_t flags = 0;

	if (otlp_traceparent_parse("0g-0af7651916cd43dd8448eb211c80319c-"
				   "b7ad6b7169203331-01",
		    trace_id,
		    span_id,
		    &flags) == OTLP_OK)
		return 0;
	if (otlp_traceparent_parse("00_0af7651916cd43dd8448eb211c80319c-"
				   "b7ad6b7169203331-01",
		    trace_id,
		    span_id,
		    &flags) == OTLP_OK)
		return 0;
	if (otlp_traceparent_parse("short", trace_id, span_id, &flags) ==
		OTLP_OK)
		return 0;
	return 1;
}

/* W3C version rules (§3.3.2):
 * - 0xff is invalid outright;
 * - version 00 is exactly 4 fields — trailing content invalid;
 * - future versions may carry extra fields — accepted and
 *   ignored (forward compatibility);
 * - hex is case-insensitive. */
static int
prop_traceparent_version_rules(uint64_t seed)
{
	(void) seed;
	uint8_t trace_id[16];
	uint8_t span_id[8];
	uint8_t flags = 0;

	/* 0xff rejected. */
	if (otlp_traceparent_parse("ff-0af7651916cd43dd8448eb211c80319c-"
				   "b7ad6b7169203331-01",
		    trace_id,
		    span_id,
		    &flags) == OTLP_OK)
		return 0;
	/* Version 00 with trailing content rejected. */
	if (otlp_traceparent_parse("00-0af7651916cd43dd8448eb211c80319c-"
				   "b7ad6b7169203331-01-junk",
		    trace_id,
		    span_id,
		    &flags) == OTLP_OK)
		return 0;
	/* Future version with extra field: accepted, fields parsed. */
	if (otlp_traceparent_parse("01-0af7651916cd43dd8448eb211c80319c-"
				   "b7ad6b7169203331-01-futurefield",
		    trace_id,
		    span_id,
		    &flags) != OTLP_OK)
		return 0;
	if (trace_id[0] != 0x0a || span_id[0] != 0xb7 || flags != 0x01)
		return 0;
	/* Uppercase hex accepted. */
	if (otlp_traceparent_parse("00-0AF7651916CD43DD8448EB211C80319C-"
				   "B7AD6B7169203331-01",
		    trace_id,
		    span_id,
		    &flags) != OTLP_OK)
		return 0;
	if (trace_id[0] != 0x0a || span_id[0] != 0xb7)
		return 0;
	return 1;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_traceparent_roundtrip,
		"prop_traceparent_roundtrip",
		500,
		1);
	failures += property_run(prop_traceparent_reject_zero,
		"prop_traceparent_reject_zero",
		1,
		1);
	failures += property_run(prop_traceparent_reject_malformed,
		"prop_traceparent_reject_malformed",
		1,
		1);
	failures += property_run(prop_traceparent_version_rules,
		"prop_traceparent_version_rules",
		1,
		1);

	if (failures)
		printf("[property] %d w3c property(ies) failed\n", failures);
	else
		printf("[property] all w3c properties passed\n");
	return failures ? 1 : 0;
}
