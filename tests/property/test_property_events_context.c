/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property tests for span events, links, trace_state, and the
 * SpanContext propagation API.
 *
 *   prop_events_field_roundtrip    — add_event emits Span.Event{11}
 *                                     with name{1} + time{2}.
 *   prop_links_field_roundtrip     — add_link emits Span.Link{13}
 *                                     with trace_id{1} + span_id{2}.
 *   prop_trace_state_field         — set_trace_state emits field 3.
 *   prop_span_clone_copies_extras  — clone copies events/links/state.
 *   prop_context_inject_extract    — context round-trips via a
 *                                     callback-based carrier.
 *   prop_context_extract_rejects_malformed — invalid traceparent
 *                                     yields has_context=false.
 */
#include "decoder.h"
#include "prng.h"
#include "property_harness.h"

#include "../src/otlp_messages.h"
#include "../src/protobuf_encode.h"
#include "../src/span_internal.h"

#include <otlp-c/context.h>
#include <otlp-c/span.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Single-level walker (same as the metrics/logs test files). */
static int
find_at_level(const uint8_t *data, size_t pos, size_t end, uint32_t fnum,
	      int *wt_out, size_t *val_pos, size_t *val_len)
{
	while (pos < end) {
		uint32_t fn = 0;
		int	    wt = 0;
		size_t	   vstart;
		size_t	   vlen = 0;
		otlp_status_t st = decode_tag(data, end, &pos, &fn, &wt);

		if (st != OTLP_OK)
			return 0;
		vstart = pos;
		if (wt == OTLP_PB_WIRE_VARINT) {
			uint64_t v;

			if (decode_varint(data, end, &pos, &v) != OTLP_OK)
				return 0;
			vlen = pos - vstart;
		} else if (wt == OTLP_PB_WIRE_FIXED64) {
			if (pos + 8 > end)
				return 0;
			pos += 8;
			vlen = 8;
		} else if (wt == OTLP_PB_WIRE_FIXED32) {
			if (pos + 4 > end)
				return 0;
			pos += 4;
			vlen = 4;
		} else if (wt == OTLP_PB_WIRE_LEN) {
			uint64_t l;

			if (decode_varint(data, end, &pos, &l) != OTLP_OK)
				return 0;
			vlen = (size_t) l;
			vstart = pos;
			if (pos + vlen > end)
				return 0;
			pos += vlen;
		} else {
			return 0;
		}
		if (fn == fnum) {
			if (wt_out && val_pos && val_len) {
				*wt_out  = wt;
				*val_pos = vstart;
				*val_len = vlen;
			}
			return 1;
		}
	}
	return 0;
}

static int
descend(const uint8_t *data, size_t *pos, size_t *end, uint32_t fnum)
{
	int    wt = 0;
	size_t vp = 0;
	size_t vl = 0;

	if (!find_at_level(data, *pos, *end, fnum, &wt, &vp, &vl))
		return 0;
	if (wt != OTLP_PB_WIRE_LEN)
		return 0;
	*pos = vp;
	*end = vp + vl;
	return 1;
}

/* ── Events / Links / TraceState ──────────────────────────────── */

static int
prop_events_field_roundtrip(uint64_t seed)
{
	struct prng	      p;
	otlp_span_t	  *span;
	struct otlp_pb_buf   buf = { 0 };
	const otlp_span_t *arr[1] = { NULL };
	char		      name[16];
	size_t		      nlen;
	int		      ok = 0;
	size_t		      pos, end, sp_pos, sp_end;
	int		      wt;
	size_t		      vp, vl;

	prng_seed(&p, seed);
	nlen = (size_t) prng_u32(&p, 12) + 1;
	for (size_t i = 0; i < nlen; i++)
		name[i] = (char)(prng_u32(&p, 94) + 33);
	name[nlen] = '\0';

	span = otlp_span_create("op");
	if (!span)
		return 0;
	otlp_span_set_start_time(span, 1);
	otlp_span_set_end_time(span, 2);
	otlp_span_add_event(span, name, 0xDEADBEEF);
	arr[0] = span;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_trace_service_request(
		    &buf, NULL, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	/* Descend: ResourceSpans{1} → ScopeSpans{2} → Span{2}. */
	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1) ||
	    !descend(buf.data, &pos, &end, 2) ||
	    !descend(buf.data, &pos, &end, 2))
		goto out_buf;

	/* Look for events{11} LEN. */
	if (!find_at_level(buf.data, pos, end, 11, &wt, &vp, &vl) ||
	    wt != OTLP_PB_WIRE_LEN)
		goto out_buf;

	/* Inside Event: name{1} LEN + time{2} FIXED64. */
	sp_pos = vp;
	sp_end = vp + vl;
	if (!find_at_level(buf.data, sp_pos, sp_end, 1, &wt, &vp, &vl) ||
	    wt != OTLP_PB_WIRE_LEN || vl != nlen ||
	    memcmp(buf.data + vp, name, nlen) != 0)
		goto out_buf;
	if (!find_at_level(buf.data, sp_pos, sp_end, 2, &wt, &vp, &vl) ||
	    wt != OTLP_PB_WIRE_FIXED64 || vl != 8)
		goto out_buf;
	{
		uint64_t got;

		memcpy(&got, buf.data + vp, 8);
		if (got != 0xDEADBEEF)
			goto out_buf;
	}
	ok = 1;

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_span_free(span);
	return ok;
}

static int
prop_links_field_roundtrip(uint64_t seed)
{
	struct prng	      p;
	otlp_span_t	  *span;
	struct otlp_pb_buf   buf = { 0 };
	const otlp_span_t *arr[1] = { NULL };
	uint8_t	      trace_id[16];
	uint8_t	      span_id[8];
	int		      ok = 0;
	size_t		      pos, end, sp_pos, sp_end;
	int		      wt;
	size_t		      vp, vl;
	size_t		      i;

	prng_seed(&p, seed);
	for (i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(&p, 256);
	for (i = 0; i < 8; i++)
		span_id[i] = (uint8_t) prng_u32(&p, 256);

	span = otlp_span_create("op");
	if (!span)
		return 0;
	otlp_span_set_start_time(span, 1);
	otlp_span_set_end_time(span, 2);
	otlp_span_add_link(span, trace_id, span_id);
	arr[0] = span;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_trace_service_request(
		    &buf, NULL, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1) ||
	    !descend(buf.data, &pos, &end, 2) ||
	    !descend(buf.data, &pos, &end, 2))
		goto out_buf;

	/* Look for links{13} LEN. */
	if (!find_at_level(buf.data, pos, end, 13, &wt, &vp, &vl) ||
	    wt != OTLP_PB_WIRE_LEN)
		goto out_buf;

	sp_pos = vp;
	sp_end = vp + vl;
	if (!find_at_level(buf.data, sp_pos, sp_end, 1, &wt, &vp, &vl) ||
	    wt != OTLP_PB_WIRE_LEN || vl != 16 ||
	    memcmp(buf.data + vp, trace_id, 16) != 0)
		goto out_buf;
	if (!find_at_level(buf.data, sp_pos, sp_end, 2, &wt, &vp, &vl) ||
	    wt != OTLP_PB_WIRE_LEN || vl != 8 ||
	    memcmp(buf.data + vp, span_id, 8) != 0)
		goto out_buf;
	ok = 1;

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_span_free(span);
	return ok;
}

static int
prop_trace_state_field(uint64_t seed)
{
	struct prng	      p;
	otlp_span_t	  *span;
	struct otlp_pb_buf   buf = { 0 };
	const otlp_span_t *arr[1] = { NULL };
	char		      state[32];
	size_t		      slen;
	int		      ok = 0;
	size_t		      pos, end;
	int		      wt;
	size_t		      vp, vl;

	prng_seed(&p, seed);
	slen = (size_t) prng_u32(&p, 28) + 1;
	for (size_t i = 0; i < slen; i++)
		state[i] = (char)(prng_u32(&p, 94) + 33);
	state[slen] = '\0';

	span = otlp_span_create("op");
	if (!span)
		return 0;
	otlp_span_set_start_time(span, 1);
	otlp_span_set_end_time(span, 2);
	otlp_span_set_trace_state(span, state);
	arr[0] = span;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_trace_service_request(
		    &buf, NULL, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1) ||
	    !descend(buf.data, &pos, &end, 2) ||
	    !descend(buf.data, &pos, &end, 2))
		goto out_buf;

	/* Look for trace_state{3} LEN. */
	if (find_at_level(buf.data, pos, end, 3, &wt, &vp, &vl) &&
	    wt == OTLP_PB_WIRE_LEN && vl == slen &&
	    memcmp(buf.data + vp, state, slen) == 0)
		ok = 1;

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_span_free(span);
	return ok;
}

static int
prop_span_clone_copies_extras(uint64_t seed)
{
	struct prng	      p;
	otlp_span_t	  *span;
	otlp_span_t	  *clone;
	uint8_t	      trace_id[16];
	uint8_t	      span_id[8];
	uint8_t	      link_trace[16];
	uint8_t	      link_span[8];
	int		      ok = 0;
	size_t		      n_events, n_links;
	const struct otlp_event *ev;
	const struct otlp_link  *lk;
	const char	     *ts;

	prng_seed(&p, seed);
	for (size_t i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(&p, 256);
	for (size_t i = 0; i < 8; i++)
		span_id[i] = (uint8_t) prng_u32(&p, 256);
	for (size_t i = 0; i < 16; i++)
		link_trace[i] = (uint8_t) prng_u32(&p, 256);
	for (size_t i = 0; i < 8; i++)
		link_span[i] = (uint8_t) prng_u32(&p, 256);

	span = otlp_span_create("op");
	if (!span)
		return 0;
	otlp_span_set_trace_id(span, trace_id);
	otlp_span_set_span_id(span, span_id);
	otlp_span_add_event(span, "ev1", 100);
	otlp_span_add_event(span, "ev2", 200);
	otlp_span_add_link(span, link_trace, link_span);
	otlp_span_set_trace_state(span, "vendor=baz");

	clone = otlp_span_clone(span);
	if (!clone)
		goto out;

	ok = 1;
	ev = otlp_span_get_events(clone, &n_events);
	if (n_events != 2 || strcmp(ev[0].name, "ev1") != 0 ||
	    ev[0].time_unix_nano != 100 || strcmp(ev[1].name, "ev2") != 0 ||
	    ev[1].time_unix_nano != 200)
		ok = 0;

	lk = otlp_span_get_links(clone, &n_links);
	if (n_links != 1 || memcmp(lk[0].trace_id, link_trace, 16) != 0 ||
	    memcmp(lk[0].span_id, link_span, 8) != 0)
		ok = 0;

	ts = otlp_span_get_trace_state(clone);
	if (!ts || strcmp(ts, "vendor=baz") != 0)
		ok = 0;

	otlp_span_free(clone);
out:
	otlp_span_free(span);
	return ok;
}

/* ── Context propagation ──────────────────────────────────────── */

#define MAX_CARRIER 4

struct test_carrier {
	struct {
		const char *key;
		char	 value[64];
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

	for (size_t i = 0; i < c->n; i++) {
		if (strcmp(c->entries[i].key, key) == 0)
			return c->entries[i].value;
	}
	return NULL;
}

static int
prop_context_inject_extract(uint64_t seed)
{
	struct prng	      p;
	otlp_span_t	  *span;
	struct test_carrier   carrier = { 0 };
	otlp_context_t	      ctx_in, ctx_out;
	uint8_t	      trace_id[16];
	uint8_t	      span_id[8];
	int		      ok = 0;

	prng_seed(&p, seed);
	for (size_t i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(&p, 256);
	for (size_t i = 0; i < 8; i++)
		span_id[i] = (uint8_t) prng_u32(&p, 256);
	/* Avoid all-zero (rejected by W3C). */
	trace_id[0] |= 1;
	span_id[0]  |= 1;

	span = otlp_span_create("op");
	if (!span)
		return 0;
	otlp_span_set_trace_id(span, trace_id);
	otlp_span_set_span_id(span, span_id);

	ctx_in = otlp_context_from_span(span);
	if (!ctx_in.has_context)
		goto out;
	if (otlp_context_inject(ctx_in, carrier_set, &carrier) != OTLP_OK)
		goto out;

	ctx_out = otlp_context_extract(carrier_get, &carrier);
	if (!ctx_out.has_context)
		goto out;

	if (memcmp(ctx_out.trace_id, trace_id, 16) == 0 &&
	    memcmp(ctx_out.span_id, span_id, 8) == 0)
		ok = 1;

out:
	otlp_span_free(span);
	return ok;
}

static int
prop_context_extract_rejects_malformed(uint64_t seed)
{
	struct test_carrier   carrier = { 0 };
	otlp_context_t	      ctx;

	(void) seed;
	/* Empty carrier → no traceparent. */
	ctx = otlp_context_extract(carrier_get, &carrier);
	if (ctx.has_context)
		return 0;

	/* Malformed traceparent → has_context=false. */
	carrier.entries[0].key = OTLP_CONTEXT_TRACEPARENT_HEADER;
	snprintf(carrier.entries[0].value, sizeof(carrier.entries[0].value),
		 "garbage");
	carrier.n = 1;
	ctx = otlp_context_extract(carrier_get, &carrier);
	if (ctx.has_context)
		return 0;

	/* Valid format but all-zero trace_id → reject. */
	snprintf(carrier.entries[0].value, sizeof(carrier.entries[0].value),
		 "00-00000000000000000000000000000000-0000000000000000-01");
	ctx = otlp_context_extract(carrier_get, &carrier);
	if (ctx.has_context)
		return 0;

	return 1;
}

static int
prop_context_tracestate_roundtrip(uint64_t seed)
{
	struct prng	      p;
	otlp_span_t	  *span;
	struct test_carrier   carrier = { 0 };
	otlp_context_t	      ctx_in, ctx_out;
	uint8_t	      trace_id[16];
	uint8_t	      span_id[8];
	const char	      *ts = "vendor1=abc123,vendor2=def456";
	int		      ok = 0;
	size_t		      i;

	prng_seed(&p, seed);
	for (i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(&p, 256);
	for (i = 0; i < 8; i++)
		span_id[i] = (uint8_t) prng_u32(&p, 256);
	trace_id[0] |= 1;
	span_id[0]  |= 1;

	span = otlp_span_create("op");
	if (!span)
		return 0;
	otlp_span_set_trace_id(span, trace_id);
	otlp_span_set_span_id(span, span_id);

	ctx_in = otlp_context_from_span(span);
	if (!ctx_in.has_context)
		goto out;
	snprintf(ctx_in.tracestate, sizeof(ctx_in.tracestate), "%s", ts);

	if (otlp_context_inject(ctx_in, carrier_set, &carrier) != OTLP_OK)
		goto out;
	ctx_out = otlp_context_extract(carrier_get, &carrier);
	if (!ctx_out.has_context)
		goto out;
	ok = (strcmp(ctx_out.tracestate, ts) == 0);

out:
	otlp_span_free(span);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_events_field_roundtrip,
				 "prop_events_field_roundtrip", 200, 1);
	failures += property_run(prop_links_field_roundtrip,
				 "prop_links_field_roundtrip", 50, 1);
	failures += property_run(prop_trace_state_field,
				 "prop_trace_state_field", 200, 1);
	failures += property_run(prop_span_clone_copies_extras,
				 "prop_span_clone_copies_extras", 50, 1);
	failures += property_run(prop_context_inject_extract,
				 "prop_context_inject_extract", 50, 1);
	failures += property_run(prop_context_extract_rejects_malformed,
				 "prop_context_extract_rejects_malformed", 1, 1);
	failures += property_run(prop_context_tracestate_roundtrip,
				 "prop_context_tracestate_roundtrip", 50, 1);

	if (failures)
		printf("[property] %d events/links/context property(ies) failed\n",
		       failures);
	else
		printf("[property] all events/links/context properties passed\n");
	return failures ? 1 : 0;
}
