/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property tests for W3C Baggage propagation.
 *
 *   prop_baggage_roundtrip           — inject context with baggage,
 *     extract from carrier, baggage matches.
 *   prop_baggage_absent_on_extract   — carrier without baggage header
 *     produces an empty baggage field.
 *   prop_baggage_with_tracestate     — both baggage and tracestate
 *     coexist on the same carrier.
 *   prop_baggage_header_constant     — OTLP_CONTEXT_BAGGAGE_HEADER
 *     is the string "baggage".
 *   prop_format_raw_matches_format   — otlp_traceparent_format_raw
 *     produces the same bytes as otlp_traceparent_format for a given
 *     span (DRY regression check).
 *
 * Uses a simple callback-based carrier (same pattern as
 * test_property_events_context.c).
 */
#include "prng.h"
#include "property_harness.h"

#include <otlp-c/context.h>
#include <otlp-c/span.h>
#include <otlp-c/w3c.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MAX_CARRIER 8

struct test_carrier {
	struct {
		const char *key;
		char	value[512];
	} entries[MAX_CARRIER];
	size_t n;
};

static otlp_status_t
carrier_set(void *ctx, const char *key, const char *value)
{
	struct test_carrier *c = ctx;

	if (c->n >= MAX_CARRIER)
		return OTLP_ERR_OVERFLOW;
	c->entries[c->n].key = key;
	snprintf(c->entries[c->n].value, sizeof(c->entries[c->n].value),
		 "%s", value);
	c->n++;
	return OTLP_OK;
}

static const char *
carrier_get(void *ctx, const char *key)
{
	struct test_carrier *c = ctx;
	size_t i;

	for (i = 0; i < c->n; i++) {
		if (strcmp(c->entries[i].key, key) == 0)
			return c->entries[i].value;
	}
	return NULL;
}

static void
fill_random_ids(struct prng *p, uint8_t trace_id[16], uint8_t span_id[8])
{
	size_t i;
	for (i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(p, 256);
	for (i = 0; i < 8; i++)
		span_id[i] = (uint8_t) prng_u32(p, 256);
	/* Avoid all-zero (rejected by W3C). */
	trace_id[0] |= 1;
	span_id[0]  |= 1;
}

static int
prop_baggage_roundtrip(uint64_t seed)
{
	struct prng	     p;
	otlp_span_t	   *span;
	struct test_carrier  carrier = { 0 };
	otlp_context_t       ctx_in, ctx_out;
	uint8_t	     trace_id[16];
	uint8_t	     span_id[8];
	const char	  *baggage = "userId=42,feature.flag=true,zone=us-west-2";
	int		   ok = 0;

	prng_seed(&p, seed);
	fill_random_ids(&p, trace_id, span_id);

	span = otlp_span_create("op");
	if (!span)
		return 0;
	otlp_span_set_trace_id(span, trace_id);
	otlp_span_set_span_id(span, span_id);

	ctx_in = otlp_context_from_span(span);
	if (!ctx_in.has_context)
		goto out;
	snprintf(ctx_in.baggage, sizeof(ctx_in.baggage), "%s", baggage);

	if (otlp_context_inject(ctx_in, carrier_set, &carrier) != OTLP_OK)
		goto out;

	ctx_out = otlp_context_extract(carrier_get, &carrier);
	if (!ctx_out.has_context)
		goto out;

	ok = (strcmp(ctx_out.baggage, baggage) == 0);

out:
	otlp_span_free(span);
	return ok;
}

static int
prop_baggage_absent_on_extract(uint64_t seed)
{
	struct prng	     p;
	otlp_span_t	   *span;
	struct test_carrier  carrier = { 0 };
	otlp_context_t       ctx_in, ctx_out;
	uint8_t	     trace_id[16];
	uint8_t	     span_id[8];
	int		   ok = 0;

	prng_seed(&p, seed);
	fill_random_ids(&p, trace_id, span_id);

	span = otlp_span_create("op");
	if (!span)
		return 0;
	otlp_span_set_trace_id(span, trace_id);
	otlp_span_set_span_id(span, span_id);

	ctx_in = otlp_context_from_span(span);
	if (!ctx_in.has_context)
		goto out;
	/* No baggage set on ctx_in. */

	if (otlp_context_inject(ctx_in, carrier_set, &carrier) != OTLP_OK)
		goto out;

	ctx_out = otlp_context_extract(carrier_get, &carrier);
	if (!ctx_out.has_context)
		goto out;

	/* Extracted baggage must be empty (no header on carrier). */
	ok = (ctx_out.baggage[0] == '\0');

out:
	otlp_span_free(span);
	return ok;
}

static int
prop_baggage_with_tracestate(uint64_t seed)
{
	struct prng	     p;
	otlp_span_t	   *span;
	struct test_carrier  carrier = { 0 };
	otlp_context_t       ctx_in, ctx_out;
	uint8_t	     trace_id[16];
	uint8_t	     span_id[8];
	const char	  *ts = "vendor1=abc,vendor2=def";
	const char	  *bg = "reqId=r-42,route=/api/v1/users";
	int		   ok = 0;

	prng_seed(&p, seed);
	fill_random_ids(&p, trace_id, span_id);

	span = otlp_span_create("op");
	if (!span)
		return 0;
	otlp_span_set_trace_id(span, trace_id);
	otlp_span_set_span_id(span, span_id);

	ctx_in = otlp_context_from_span(span);
	if (!ctx_in.has_context)
		goto out;
	snprintf(ctx_in.tracestate, sizeof(ctx_in.tracestate), "%s", ts);
	snprintf(ctx_in.baggage, sizeof(ctx_in.baggage), "%s", bg);

	if (otlp_context_inject(ctx_in, carrier_set, &carrier) != OTLP_OK)
		goto out;
	ctx_out = otlp_context_extract(carrier_get, &carrier);
	if (!ctx_out.has_context)
		goto out;

	ok = (strcmp(ctx_out.tracestate, ts) == 0 &&
	      strcmp(ctx_out.baggage, bg) == 0);

out:
	otlp_span_free(span);
	return ok;
}

static int
prop_baggage_header_constant(uint64_t seed)
{
	(void) seed;
	return strcmp(OTLP_CONTEXT_BAGGAGE_HEADER, "baggage") == 0;
}

/* DRY regression check: otlp_traceparent_format_raw produces the same
 * output as otlp_traceparent_format for a given span. */
static int
prop_format_raw_matches_format(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	uint8_t    trace_id[16];
	uint8_t    span_id[8];
	char       buf_span[OTLP_TRACEPARENT_BUF_SIZE];
	char       buf_raw[OTLP_TRACEPARENT_BUF_SIZE];
	otlp_status_t st;
	int        ok = 0;

	prng_seed(&p, seed);
	fill_random_ids(&p, trace_id, span_id);

	span = otlp_span_create("op");
	if (!span)
		return 0;
	otlp_span_set_trace_id(span, trace_id);
	otlp_span_set_span_id(span, span_id);

	st = otlp_traceparent_format(span, true, buf_span, sizeof(buf_span), NULL);
	if (st != OTLP_OK)
		goto out;
	st = otlp_traceparent_format_raw(trace_id, span_id, true,
					 buf_raw, sizeof(buf_raw), NULL);
	if (st != OTLP_OK)
		goto out;

	ok = (strcmp(buf_span, buf_raw) == 0);

out:
	otlp_span_free(span);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_baggage_roundtrip,
				 "prop_baggage_roundtrip", 50, 1);
	failures += property_run(prop_baggage_absent_on_extract,
				 "prop_baggage_absent_on_extract", 20, 1);
	failures += property_run(prop_baggage_with_tracestate,
				 "prop_baggage_with_tracestate", 30, 1);
	failures += property_run(prop_baggage_header_constant,
				 "prop_baggage_header_constant", 1, 1);
	failures += property_run(prop_format_raw_matches_format,
				 "prop_format_raw_matches_format", 50, 1);

	if (failures)
		printf("[property] %d baggage property(ies) failed\n", failures);
	else
		printf("[property] all baggage properties passed\n");
	return failures ? 1 : 0;
}
