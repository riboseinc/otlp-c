/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property tests for span events, links, trace_state, and the
 * SpanContext propagation API.
 *
 *   prop_events_field_roundtrip    — add_event emits Span.Event{11}
 *                                     with time{1} + name{2}.
 *   prop_links_field_roundtrip     — add_link emits Span.Link{13}
 *                                     with trace_id{1} + span_id{2}.
 *   prop_trace_state_field         — set_trace_state emits field 3.
 *   prop_span_clone_copies_extras  — clone copies events/links/state.
 *   prop_span_clone_preserves_evlink_attrs — clone deep-copies
 *                                     event/link attributes (typed).
 *   prop_event_link_typed_attrs_wire — typed (int64/double/bool/
 *                                     bytes) event/link attributes
 *                                     land as the right AnyValue
 *                                     oneof member on the wire.
 *   prop_context_inject_extract    — context round-trips via a
 *                                     callback-based carrier.
 *   prop_context_extract_rejects_malformed — invalid traceparent
 *                                     yields has_context=false.
 */
#include "decoder.h"
#include "walker.h"
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
/* ── Events / Links / TraceState ──────────────────────────────── */

static int
prop_events_field_roundtrip(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	struct otlp_pb_buf buf = { 0 };
	const otlp_span_t *arr[1] = { NULL };
	char name[16];
	size_t nlen;
	int ok = 0;
	size_t pos, end, sp_pos, sp_end;
	int wt;
	size_t vp, vl;

	prng_seed(&p, seed);
	nlen = (size_t) prng_u32(&p, 12) + 1;
	for (size_t i = 0; i < nlen; i++)
		name[i] = (char) (prng_u32(&p, 94) + 33);
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
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	/* Descend: ResourceSpans{1} → ScopeSpans{2} → Span{2}. */
	pos = 0;
	end = buf.len;
	if (!walker_descend(buf.data, &pos, &end, 1) ||
		!walker_descend(buf.data, &pos, &end, 2) ||
		!walker_descend(buf.data, &pos, &end, 2))
		goto out_buf;

	/* Look for events{11} LEN. */
	if (!walker_find_at_level(buf.data, pos, end, 11, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_LEN)
		goto out_buf;

	/* Inside Event: time{1} FIXED64 + name{2} LEN. The field order
	 * matches opentelemetry-proto (time_unix_nano=1, name=2). */
	sp_pos = vp;
	sp_end = vp + vl;
	if (!walker_find_at_level(buf.data, sp_pos, sp_end, 1, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_FIXED64 || vl != 8)
		goto out_buf;
	{
		uint64_t got;

		memcpy(&got, buf.data + vp, 8);
		if (got != 0xDEADBEEF)
			goto out_buf;
	}
	if (!walker_find_at_level(buf.data, sp_pos, sp_end, 2, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_LEN || vl != nlen ||
		memcmp(buf.data + vp, name, nlen) != 0)
		goto out_buf;
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
	struct prng p;
	otlp_span_t *span;
	struct otlp_pb_buf buf = { 0 };
	const otlp_span_t *arr[1] = { NULL };
	uint8_t trace_id[16];
	uint8_t span_id[8];
	int ok = 0;
	size_t pos, end, sp_pos, sp_end;
	int wt;
	size_t vp, vl;
	size_t i;

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
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!walker_descend(buf.data, &pos, &end, 1) ||
		!walker_descend(buf.data, &pos, &end, 2) ||
		!walker_descend(buf.data, &pos, &end, 2))
		goto out_buf;

	/* Look for links{13} LEN. */
	if (!walker_find_at_level(buf.data, pos, end, 13, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_LEN)
		goto out_buf;

	sp_pos = vp;
	sp_end = vp + vl;
	if (!walker_find_at_level(buf.data, sp_pos, sp_end, 1, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_LEN || vl != 16 ||
		memcmp(buf.data + vp, trace_id, 16) != 0)
		goto out_buf;
	if (!walker_find_at_level(buf.data, sp_pos, sp_end, 2, &wt, &vp, &vl) ||
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
	struct prng p;
	otlp_span_t *span;
	struct otlp_pb_buf buf = { 0 };
	const otlp_span_t *arr[1] = { NULL };
	char state[32];
	size_t slen;
	int ok = 0;
	size_t pos, end;
	int wt;
	size_t vp, vl;

	prng_seed(&p, seed);
	slen = (size_t) prng_u32(&p, 28) + 1;
	for (size_t i = 0; i < slen; i++)
		state[i] = (char) (prng_u32(&p, 94) + 33);
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
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!walker_descend(buf.data, &pos, &end, 1) ||
		!walker_descend(buf.data, &pos, &end, 2) ||
		!walker_descend(buf.data, &pos, &end, 2))
		goto out_buf;

	/* Look for trace_state{3} LEN. */
	if (walker_find_at_level(buf.data, pos, end, 3, &wt, &vp, &vl) &&
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
	struct prng p;
	otlp_span_t *span;
	otlp_span_t *clone;
	uint8_t trace_id[16];
	uint8_t span_id[8];
	uint8_t link_trace[16];
	uint8_t link_span[8];
	int ok = 0;
	size_t n_events, n_links;
	const struct otlp_event *ev;
	const struct otlp_link *lk;
	const char *ts;

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

struct test_carrier
{
	struct
	{
		const char *key;
		char value[64];
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
	snprintf(c->entries[c->n].value,
		sizeof(c->entries[c->n].value),
		"%s",
		value);
	c->n++;
	return OTLP_OK;
}

static const char *
carrier_get(void *ctx, const char *key)
{
	struct test_carrier *c = ctx;

	for (size_t i = 0; i < c->n; i++)
	{
		if (strcmp(c->entries[i].key, key) == 0)
			return c->entries[i].value;
	}
	return NULL;
}

static int
prop_context_inject_extract(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	struct test_carrier carrier = { 0 };
	otlp_context_t ctx_in, ctx_out;
	uint8_t trace_id[16];
	uint8_t span_id[8];
	int ok = 0;

	prng_seed(&p, seed);
	for (size_t i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(&p, 256);
	for (size_t i = 0; i < 8; i++)
		span_id[i] = (uint8_t) prng_u32(&p, 256);
	/* Avoid all-zero (rejected by W3C). */
	trace_id[0] |= 1;
	span_id[0] |= 1;

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
	struct test_carrier carrier = { 0 };
	otlp_context_t ctx;

	(void) seed;
	/* Empty carrier → no traceparent. */
	ctx = otlp_context_extract(carrier_get, &carrier);
	if (ctx.has_context)
		return 0;

	/* Malformed traceparent → has_context=false. */
	carrier.entries[0].key = OTLP_CONTEXT_TRACEPARENT_HEADER;
	snprintf(carrier.entries[0].value,
		sizeof(carrier.entries[0].value),
		"garbage");
	carrier.n = 1;
	ctx = otlp_context_extract(carrier_get, &carrier);
	if (ctx.has_context)
		return 0;

	/* Valid format but all-zero trace_id → reject. */
	snprintf(carrier.entries[0].value,
		sizeof(carrier.entries[0].value),
		"00-00000000000000000000000000000000-0000000000000000-01");
	ctx = otlp_context_extract(carrier_get, &carrier);
	if (ctx.has_context)
		return 0;

	return 1;
}

static int
prop_context_tracestate_roundtrip(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	struct test_carrier carrier = { 0 };
	otlp_context_t ctx_in, ctx_out;
	uint8_t trace_id[16];
	uint8_t span_id[8];
	const char *ts = "vendor1=abc123,vendor2=def456";
	int ok = 0;
	size_t i;

	prng_seed(&p, seed);
	for (i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(&p, 256);
	for (i = 0; i < 8; i++)
		span_id[i] = (uint8_t) prng_u32(&p, 256);
	trace_id[0] |= 1;
	span_id[0] |= 1;

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

/* Regression: clone must preserve event AND link attributes.
 * Before v0.5.33, otlp_span_clone copied events with only name +
 * time — attributes were silently dropped (data loss). */
static int
prop_span_clone_preserves_evlink_attrs(uint64_t seed)
{
	otlp_span_t *span;
	otlp_span_t *clone;
	const struct otlp_event *ev;
	const struct otlp_link *lk;
	size_t n;
	int ok = 0;

	(void) seed;
	span = otlp_span_create("op");
	if (!span)
		return 0;

	/* Add event with string + int attributes. */
	otlp_span_add_event(span, "cache_miss", 42);
	otlp_span_set_event_attribute_string(span, "key", "user_42");
	otlp_span_set_event_attribute_int(span, "attempts", 7);

	/* Add link with string + bytes attributes. */
	{
		uint8_t tid[16] = { 1 };
		uint8_t sid[8] = { 2 };

		otlp_span_add_link(span, tid, sid);
	}
	otlp_span_set_link_attribute_string(span, "reason", "follows_from");
	{
		const uint8_t ref[3] = { 0x01, 0x02, 0xfe };

		otlp_span_set_link_attribute_bytes(span, "ref", ref, 3);
	}

	clone = otlp_span_clone(span);
	if (!clone)
		goto out;

	ok = 1;
	/* Verify event has the attributes. */
	ev = otlp_span_get_events(clone, &n);
	if (n != 1 || ev[0].attrs.n != 2)
		ok = 0;
	else if (strcmp(ev[0].attrs.items[0].key, "key") != 0 ||
		strcmp(ev[0].attrs.items[0].v.string_val, "user_42") != 0)
		ok = 0;
	else if (ev[0].attrs.items[1].type != OTLP_ATTR_INT64 ||
		ev[0].attrs.items[1].v.int64_val != 7)
		ok = 0;

	/* Verify link has the attributes. */
	lk = otlp_span_get_links(clone, &n);
	if (n != 1 || lk[0].attrs.n != 2)
		ok = 0;
	else if (strcmp(lk[0].attrs.items[0].key, "reason") != 0 ||
		strcmp(lk[0].attrs.items[0].v.string_val, "follows_from") != 0)
		ok = 0;
	else if (lk[0].attrs.items[1].type != OTLP_ATTR_BYTES ||
		lk[0].attrs.items[1].v.bytes_val.len != 3 ||
		lk[0].attrs.items[1].v.bytes_val.data[2] != 0xfe)
		ok = 0;

out:
	if (clone)
		otlp_span_free(clone);
	otlp_span_free(span);
	return ok;
}

/* Verify a typed attribute inside an Event{11} / Link{13} payload:
 * attributes → KeyValue → value{2} → AnyValue oneof member with
 * the expected field number + wire type + value. attr_fnum is 3
 * for Event and 6 for Link (upstream field order differs). */
static int
verify_typed_attr_at_level(const uint8_t *data,
	size_t pos,
	size_t end,
	uint32_t attr_fnum,
	unsigned type,
	uint64_t vbits,
	const uint8_t *bytes,
	size_t nbytes)
{
	int wt;
	size_t vp, vl;
	/* AnyValue oneof: bool{2} VARINT, int64{3} VARINT,
	 * double{4} FIXED64, bytes{7} LEN. */
	uint32_t fi = (type == 0) ? 3 : (type == 1) ? 4 : (type == 2) ? 2 : 7;
	int exp_wt = (type == 1) ? OTLP_PB_WIRE_FIXED64
		: (type == 3)	 ? OTLP_PB_WIRE_LEN
				 : OTLP_PB_WIRE_VARINT;
	size_t kp, ke, ap, ae;

	if (!walker_find_at_level(data, pos, end, attr_fnum, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_LEN)
		return 0;
	kp = vp;
	ke = vp + vl;
	if (!walker_find_at_level(data, kp, ke, 2, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_LEN)
		return 0;
	ap = vp;
	ae = vp + vl;
	if (!walker_find_at_level(data, ap, ae, fi, &wt, &vp, &vl) ||
		wt != exp_wt)
		return 0;
	if (type == 0 || type == 2)
	{
		size_t p2 = vp;
		uint64_t got;

		return decode_varint(data, ae, &p2, &got) == OTLP_OK &&
			got == (type == 0 ? vbits : (vbits & 1));
	}
	if (type == 1)
	{
		uint64_t got = 0;
		size_t i;

		if (vl != 8)
			return 0;
		for (i = 0; i < 8; i++)
			got |= (uint64_t) data[vp + i] << (8 * i);
		return got == vbits;
	}
	return vl == nbytes && memcmp(data + vp, bytes, nbytes) == 0;
}

/* Typed event/link attributes (int64/double/bool/bytes, cycled by
 * seed) land on the wire as the right AnyValue oneof member. */
static int
prop_event_link_typed_attrs_wire(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	struct otlp_pb_buf buf = { 0 };
	const otlp_span_t *arr[1] = { NULL };
	union
	{
		uint64_t u;
		double d;
	} v;
	uint8_t bytes[5];
	unsigned type;
	int ok = 0;
	size_t pos, end;
	int wt;
	size_t vp, vl;
	size_t i;
	uint8_t tid[16] = { 1 };
	uint8_t sid[8] = { 2 };

	prng_seed(&p, seed);
	v.u = prng_next(&p);
	for (i = 0; i < sizeof(bytes); i++)
		bytes[i] = (uint8_t) prng_next(&p);
	type = (unsigned) (seed % 4);

	span = otlp_span_create("op");
	if (!span)
		return 0;
	otlp_span_add_event(span, "e", 1);
	otlp_span_add_link(span, tid, sid);
	switch (type)
	{
		case 0:
			otlp_span_set_event_attribute_int(
				span, "k", (int64_t) v.u);
			otlp_span_set_link_attribute_int(
				span, "k", (int64_t) v.u);
			break;
		case 1:
			otlp_span_set_event_attribute_double(span, "k", v.d);
			otlp_span_set_link_attribute_double(span, "k", v.d);
			break;
		case 2:
			otlp_span_set_event_attribute_bool(span, "k", v.u & 1);
			otlp_span_set_link_attribute_bool(span, "k", v.u & 1);
			break;
		default:
			otlp_span_set_event_attribute_bytes(
				span, "k", bytes, sizeof(bytes));
			otlp_span_set_link_attribute_bytes(
				span, "k", bytes, sizeof(bytes));
			break;
	}
	arr[0] = span;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_trace_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!walker_descend(buf.data, &pos, &end, 1) ||
		!walker_descend(buf.data, &pos, &end, 2) ||
		!walker_descend(buf.data, &pos, &end, 2))
		goto out_buf;

	ok = 0;
	/* Event{11}: attributes at field 3 (opentelemetry-proto Event
	 * field order: time=1, name=2, attributes=3). */
	if (walker_find_at_level(buf.data, pos, end, 11, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_LEN)
		ok = verify_typed_attr_at_level(buf.data,
			vp,
			vp + vl,
			3,
			type,
			v.u,
			bytes,
			sizeof(bytes));
	/* Link{13}: attributes at field 4 (trace_id=1, span_id=2,
	 * trace_state=3, attributes=4, dropped=5, flags=6). */
	if (ok && walker_find_at_level(buf.data, pos, end, 13, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_LEN)
		ok = verify_typed_attr_at_level(buf.data,
			vp,
			vp + vl,
			4,
			type,
			v.u,
			bytes,
			sizeof(bytes));
	else if (ok)
		ok = 0;

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_span_free(span);
	return ok;
}

/* Control bytes in tracestate/baggage (not just CRLF) are
 * rejected on extract: any byte < 0x20 or 0x7f would produce an
 * invalid outgoing header on inject (CWE-93 hardening, v0.5.81). */
static int
prop_context_rejects_control_bytes(uint64_t seed)
{
	struct test_carrier c2 = { 0 };
	otlp_context_t in, ctx;
	otlp_span_t *span;
	bool found_ts = false;

	(void) seed;
	span = otlp_span_create("op");
	if (!span)
		return 0;
	otlp_span_set_trace_id(span,
		(const uint8_t *) "\x01\x02\x03\x04\x05\x06\x07\x08"
				  "\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10");
	otlp_span_set_span_id(
		span, (const uint8_t *) "\x11\x12\x13\x14\x15\x16\x17\x18");
	in = otlp_context_from_span(span);
	otlp_span_free(span);
	if (!in.has_context)
		return 0;
	snprintf(in.tracestate, sizeof(in.tracestate), "vendor=abc");
	if (otlp_context_inject(in, carrier_set, &c2) != OTLP_OK)
		return 0;
	/* Sanity: the carrier holds a tracestate entry. */
	for (size_t i = 0; i < c2.n; i++)
		if (strcmp(c2.entries[i].key, "tracestate") == 0)
			found_ts = true;
	if (!found_ts)
		return 0;
	/* Corrupt it with a 0x01 control byte. */
	for (size_t i = 0; i < c2.n; i++)
	{
		if (strcmp(c2.entries[i].key, "tracestate") == 0)
		{
			c2.entries[i].value[9] = 0x01;
			c2.entries[i].value[10] = 'x';
			c2.entries[i].value[11] = '\0';
			break;
		}
	}
	ctx = otlp_context_extract(carrier_get, &c2);
	/* The context extracts, but the tainted tracestate is
	 * dropped. */
	if (!ctx.has_context)
		return 0;
	if (ctx.tracestate[0] != '\0')
		return 0;
	return 1;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_events_field_roundtrip,
		"prop_events_field_roundtrip",
		200,
		1);
	failures += property_run(prop_links_field_roundtrip,
		"prop_links_field_roundtrip",
		50,
		1);
	failures += property_run(
		prop_trace_state_field, "prop_trace_state_field", 200, 1);
	failures += property_run(prop_span_clone_copies_extras,
		"prop_span_clone_copies_extras",
		50,
		1);
	failures += property_run(prop_span_clone_preserves_evlink_attrs,
		"prop_span_clone_preserves_evlink_attrs",
		50,
		1);
	failures += property_run(prop_event_link_typed_attrs_wire,
		"prop_event_link_typed_attrs_wire",
		200,
		1);
	failures += property_run(prop_context_inject_extract,
		"prop_context_inject_extract",
		50,
		1);
	failures += property_run(prop_context_extract_rejects_malformed,
		"prop_context_extract_rejects_malformed",
		1,
		1);
	failures += property_run(prop_context_tracestate_roundtrip,
		"prop_context_tracestate_roundtrip",
		50,
		1);
	failures += property_run(prop_context_rejects_control_bytes,
		"prop_context_rejects_control_bytes",
		1,
		1);

	if (failures)
		printf("[property] %d events/links/context property(ies) "
		       "failed\n",
			failures);
	else
		printf("[property] all events/links/context properties "
		       "passed\n");
	return failures ? 1 : 0;
}
