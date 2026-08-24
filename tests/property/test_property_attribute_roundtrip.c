/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Property tests for span attributes — round-trip via internal
 * accessors.
 *
 *   prop_attr_string_roundtrip — string attr preserves value.
 *   prop_attr_int64_roundtrip — int64 attr preserves value.
 *   prop_attr_double_roundtrip — double attr preserves value.
 *   prop_attr_bool_roundtrip  — bool attr preserves value.
 *   prop_attr_bytes_roundtrip — bytes attr preserves value + length.
 *   prop_attr_overflow_rejected — past cap (128), returns OTLP_ERR_OVERFLOW.
 *   prop_attr_lookup_by_index — get_attrs returns them in set order.
 *   prop_attr_upsert_last_write_wins — re-setting a key replaces the
 *                                     value in place (no duplicates).
 *   prop_attr_upsert_at_cap  — at cap a new key overflows but an
 *                             overwrite of an existing key succeeds.
 */
#include "decoder.h"
#include "prng.h"
#include "property_harness.h"
#include "walker.h"

#include <otlp-c/span.h>

#include "../src/otlp_messages.h"
#include "../src/protobuf_encode.h"
#include "../src/span_internal.h"

#include <stdint.h>
#include <string.h>

#define OTLP_SPAN_MAX_ATTRIBUTES 128 /* must match span.c */

/* ── Properties ───────────────────────────────────────────────── */

static int
prop_attr_string_roundtrip(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	char key[32];
	char val[64];
	size_t n;
	const struct otlp_attribute *attrs;
	int ok = 0;

	prng_seed(&p, seed);
	snprintf(key, sizeof(key), "k%llu", (unsigned long long) prng_next(&p));
	/* Build a random ASCII string of length 1..60. */
	size_t vlen = (size_t) prng_u32(&p, 60) + 1;
	for (size_t i = 0; i < vlen; i++)
		val[i] = (char) (prng_u32(&p, 94) + 33);
	val[vlen] = '\0';

	span = otlp_span_create("s");
	if (!span)
		return 0;
	if (otlp_span_set_attribute_string(span, key, val) != OTLP_OK)
		goto out;

	attrs = otlp_span_get_attrs(span, &n);
	if (n != 1 || !attrs)
		goto out;
	if (attrs[0].type != OTLP_ATTR_STRING)
		goto out;
	if (!str_eq(attrs[0].key, key))
		goto out;
	ok = str_eq(attrs[0].v.string_val, val) ? 1 : 0;

out:
	otlp_span_free(span);
	return ok;
}

static int
prop_attr_int64_roundtrip(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	uint64_t v;
	size_t n;
	const struct otlp_attribute *attrs;
	int ok = 0;

	prng_seed(&p, seed);
	v = prng_next(&p);

	span = otlp_span_create("s");
	if (!span)
		return 0;
	if (otlp_span_set_attribute_int(span, "n", (int64_t) v) != OTLP_OK)
		goto out;

	attrs = otlp_span_get_attrs(span, &n);
	if (n != 1 || attrs[0].type != OTLP_ATTR_INT64)
		goto out;
	ok = (attrs[0].v.int64_val == (int64_t) v);

out:
	otlp_span_free(span);
	return ok;
}

static int
prop_attr_double_roundtrip(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	uint64_t bits;
	double val;
	size_t n;
	const struct otlp_attribute *attrs;
	int ok = 0;

	prng_seed(&p, seed);
	bits = prng_next(&p);
	memcpy(&val, &bits, sizeof(val));

	span = otlp_span_create("s");
	if (!span)
		return 0;
	if (otlp_span_set_attribute_double(span, "d", val) != OTLP_OK)
		goto out;

	attrs = otlp_span_get_attrs(span, &n);
	if (n != 1 || attrs[0].type != OTLP_ATTR_DOUBLE)
		goto out;
	ok = (attrs[0].v.double_val == val);

out:
	otlp_span_free(span);
	return ok;
}

static int
prop_attr_bool_roundtrip(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	bool val;
	size_t n;
	const struct otlp_attribute *attrs;
	int ok = 0;

	prng_seed(&p, seed);
	val = (prng_next(&p) & 1) ? true : false;

	span = otlp_span_create("s");
	if (!span)
		return 0;
	if (otlp_span_set_attribute_bool(span, "b", val) != OTLP_OK)
		goto out;

	attrs = otlp_span_get_attrs(span, &n);
	if (n != 1 || attrs[0].type != OTLP_ATTR_BOOL)
		goto out;
	ok = (attrs[0].v.bool_val == val);

out:
	otlp_span_free(span);
	return ok;
}

static int
prop_attr_bytes_roundtrip(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	uint8_t buf[256];
	size_t len;
	size_t n;
	const struct otlp_attribute *attrs;
	int ok = 0;

	prng_seed(&p, seed);
	len = (size_t) prng_u32(&p, (uint32_t) sizeof(buf)) + 1;
	for (size_t i = 0; i < len; i++)
		buf[i] = (uint8_t) prng_next(&p);

	span = otlp_span_create("s");
	if (!span)
		return 0;
	if (otlp_span_set_attribute_bytes(span, "by", buf, len) != OTLP_OK)
		goto out;

	attrs = otlp_span_get_attrs(span, &n);
	if (n != 1 || attrs[0].type != OTLP_ATTR_BYTES)
		goto out;
	if (attrs[0].v.bytes_val.len != len)
		goto out;
	ok = (memcmp(attrs[0].v.bytes_val.data, buf, len) == 0);

out:
	otlp_span_free(span);
	return ok;
}

static int
prop_attr_overflow_rejected(uint64_t seed)
{
	otlp_span_t *span;
	int ok = 0;
	otlp_status_t last = OTLP_OK;

	(void) seed;
	span = otlp_span_create("s");
	if (!span)
		return 0;

	for (int i = 0; i < OTLP_SPAN_MAX_ATTRIBUTES; i++)
	{
		char k[16];
		snprintf(k, sizeof(k), "k%d", i);
		last = otlp_span_set_attribute_int(span, k, i);
		if (last != OTLP_OK)
			goto out;
	}
	/* The next one must be rejected. */
	last = otlp_span_set_attribute_int(span, "overflow", 0);
	ok = (last == OTLP_ERR_OVERFLOW);

out:
	otlp_span_free(span);
	return ok;
}

static int
prop_attr_lookup_by_index(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	size_t n;
	const struct otlp_attribute *attrs;
	int ok = 0;

	(void) seed;
	prng_seed(&p, 42);

	span = otlp_span_create("s");
	if (!span)
		return 0;

	char k1[16], k2[16], k3[16];
	snprintf(k1, sizeof(k1), "first");
	snprintf(k2, sizeof(k2), "second");
	snprintf(k3, sizeof(k3), "third");

	if (otlp_span_set_attribute_int(span, k1, 10) != OTLP_OK)
		goto out;
	if (otlp_span_set_attribute_int(span, k2, 20) != OTLP_OK)
		goto out;
	if (otlp_span_set_attribute_int(span, k3, 30) != OTLP_OK)
		goto out;

	attrs = otlp_span_get_attrs(span, &n);
	if (n != 3)
		goto out;
	if (!str_eq(attrs[0].key, "first") || attrs[0].v.int64_val != 10)
		goto out;
	if (!str_eq(attrs[1].key, "second") || attrs[1].v.int64_val != 20)
		goto out;
	if (!str_eq(attrs[2].key, "third") || attrs[2].v.int64_val != 30)
		goto out;
	ok = 1;

out:
	otlp_span_free(span);
	return ok;
}

/* Upsert: attributes are a map — re-setting a key replaces the
 * value in place (slot position preserved, count unchanged, type
 * may change). Repeated sets must never produce duplicate keys. */
static int
prop_attr_upsert_last_write_wins(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	size_t n;
	const struct otlp_attribute *attrs;
	uint64_t v2;
	int ok = 0;

	prng_seed(&p, seed);
	v2 = prng_next(&p);
	span = otlp_span_create("s");
	if (!span)
		return 0;

	if (otlp_span_set_attribute_string(span, "k", "first") != OTLP_OK)
		goto out;
	if (otlp_span_set_attribute_int(span, "other", 1) != OTLP_OK)
		goto out;
	/* Same key, new type + value. */
	if (otlp_span_set_attribute_int(span, "k", (int64_t) v2) != OTLP_OK)
		goto out;

	attrs = otlp_span_get_attrs(span, &n);
	if (n != 2)
		goto out;
	if (!str_eq(attrs[0].key, "k"))
		goto out;
	if (attrs[0].type != OTLP_ATTR_INT64 ||
		attrs[0].v.int64_val != (int64_t) v2)
		goto out;
	if (!str_eq(attrs[1].key, "other") || attrs[1].v.int64_val != 1)
		goto out;
	ok = 1;

out:
	otlp_span_free(span);
	return ok;
}

/* At cap, a NEW key overflows but overwriting an EXISTING key
 * still succeeds (a map, not a fixed set of slots). */
static int
prop_attr_upsert_at_cap(uint64_t seed)
{
	otlp_span_t *span;
	size_t n;
	int ok = 0;

	(void) seed;
	span = otlp_span_create("s");
	if (!span)
		return 0;

	for (int i = 0; i < OTLP_SPAN_MAX_ATTRIBUTES; i++)
	{
		char k[16];
		snprintf(k, sizeof(k), "k%d", i);
		if (otlp_span_set_attribute_int(span, k, i) != OTLP_OK)
			goto out;
	}
	/* New key at cap: rejected. */
	if (otlp_span_set_attribute_int(span, "overflow", 0) !=
		OTLP_ERR_OVERFLOW)
		goto out;
	/* Existing key at cap: replaced. */
	if (otlp_span_set_attribute_string(span, "k7", "replaced") != OTLP_OK)
		goto out;

	otlp_span_get_attrs(span, &n);
	ok = (n == OTLP_SPAN_MAX_ATTRIBUTES);

out:
	otlp_span_free(span);
	return ok;
}

/* Duplicate keys must never reach the wire (OTLP data model:
 * keys MUST be unique; OTel API: last write wins). A span whose
 * key was set twice must encode byte-identically to one whose
 * key was set once with the final value. */
static int
prop_attr_upsert_wire_identical(uint64_t seed)
{
	struct prng p;
	otlp_span_t *twice, *once;
	struct otlp_pb_buf buf_twice = { 0 }, buf_once = { 0 };
	const otlp_span_t *arr[1] = { NULL };
	int64_t v;
	int ok = 0;

	prng_seed(&p, seed);
	v = (int64_t) prng_next(&p);

	twice = otlp_span_create("s");
	once = otlp_span_create("s");
	if (!twice || !once)
		goto out;
	if (otlp_span_set_attribute_int(twice, "k", 1) != OTLP_OK)
		goto out;
	if (otlp_span_set_attribute_int(twice, "k", v) != OTLP_OK)
		goto out;
	if (otlp_span_set_attribute_int(once, "k", v) != OTLP_OK)
		goto out;
	if (otlp_pb_buf_init(&buf_twice, 0) != OTLP_OK ||
		otlp_pb_buf_init(&buf_once, 0) != OTLP_OK)
		goto out;
	arr[0] = twice;
	if (otlp_encode_export_trace_service_request(
		    &buf_twice, NULL, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out;
	arr[0] = once;
	if (otlp_encode_export_trace_service_request(
		    &buf_once, NULL, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out;
	ok = (buf_twice.len == buf_once.len &&
		memcmp(buf_twice.data, buf_once.data, buf_twice.len) == 0);

out:
	otlp_pb_buf_free(&buf_twice);
	otlp_pb_buf_free(&buf_once);
	otlp_span_free(twice);
	otlp_span_free(once);
	return ok;
}

/* ArrayValue on the wire: AnyValue array_value{5} contains N
 * values{1} submessages, each an AnyValue with its own oneof
 * member (item 0: int64, item 1: string). */
static int
prop_attr_array_wire(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	struct otlp_pb_buf buf = { 0 };
	const otlp_span_t *arr[1] = { NULL };
	otlp_value_t items_in[2] = {
		{ .type = OTLP_VALUE_INT64, .v = { .int64_val = 0 } },
		{ .type = OTLP_VALUE_STRING, .v = { .string_val = NULL } },
	};
	uint64_t v;
	char sval[16];
	size_t slen;
	int ok = 0;
	size_t pos, end, ip, ie;
	int wt;
	size_t vp, vl;
	int n_items = 0;

	prng_seed(&p, seed);
	v = prng_next(&p);
	slen = (size_t) prng_u32(&p, 10) + 1;
	for (size_t i = 0; i < slen; i++)
		sval[i] = (char) (prng_u32(&p, 94) + 33);
	sval[slen] = '\0';
	items_in[0].v.int64_val = (int64_t) v;
	items_in[1].v.string_val = sval;

	span = otlp_span_create("s");
	if (!span)
		return 0;
	if (otlp_span_set_attribute_array(span, "k", items_in, 2) != OTLP_OK)
		goto out;
	arr[0] = span;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_trace_service_request(
		    &buf, NULL, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!walker_descend(buf.data, &pos, &end, 1) || /* ResourceSpans */
		!walker_descend(buf.data, &pos, &end, 2) || /* ScopeSpans */
		!walker_descend(buf.data, &pos, &end, 2) || /* Span */
		!walker_descend(buf.data, &pos, &end, 9) || /* attributes */
		!walker_descend(buf.data, &pos, &end, 2) || /* value */
		!walker_descend(buf.data, &pos, &end, 5)) /* array_value */
		goto out_buf;

	/* Iterate values{1} inside the ArrayValue: each match's
	 * payload ends exactly where the next field's tag begins. */
	ip = pos;
	ie = end;
	while (n_items < 2 && ip < ie &&
		walker_find_at_level(buf.data, ip, ie, 1, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_LEN)
	{
		size_t item_pos = vp, item_end = vp + vl;

		if (n_items == 0)
		{
			if (!walker_find_at_level(buf.data,
				    item_pos,
				    item_end,
				    3,
				    &wt,
				    &vp,
				    &vl) ||
				wt != OTLP_PB_WIRE_VARINT)
				goto out_buf;
			{
				size_t pv = vp;
				uint64_t got = 0;

				if (decode_varint(
					    buf.data, item_end, &pv, &got) !=
						OTLP_OK ||
					got != v)
					goto out_buf;
			}
		}
		else
		{
			if (!walker_find_at_level(buf.data,
				    item_pos,
				    item_end,
				    1,
				    &wt,
				    &vp,
				    &vl) ||
				wt != OTLP_PB_WIRE_LEN || vl != slen ||
				memcmp(buf.data + vp, sval, slen) != 0)
				goto out_buf;
		}
		n_items++;
		ip = item_end;
	}
	ok = (n_items == 2);

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_span_free(span);
	return ok;
}

/* KeyValueList on the wire: AnyValue kvlist_value{6} contains N
 * values{1} KeyValue submessages (key{1} + value{2}). */
static int
prop_attr_kvlist_wire(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	struct otlp_pb_buf buf = { 0 };
	const otlp_span_t *arr[1] = { NULL };
	otlp_kv_t kvs_in[2] = {
		{ .key = "k1",
			.value = { .type = OTLP_VALUE_INT64,
				.v = { .int64_val = 0 } } },
		{ .key = "k2",
			.value = { .type = OTLP_VALUE_BOOL,
				.v = { .bool_val = true } } },
	};
	uint64_t v;
	int ok = 0;
	size_t pos, end;
	int wt;
	size_t vp, vl;

	prng_seed(&p, seed);
	v = prng_next(&p);
	kvs_in[0].value.v.int64_val = (int64_t) v;

	span = otlp_span_create("s");
	if (!span)
		return 0;
	if (otlp_span_set_attribute_kvlist(span, "k", kvs_in, 2) != OTLP_OK)
		goto out;
	arr[0] = span;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_trace_service_request(
		    &buf, NULL, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!walker_descend(buf.data, &pos, &end, 1) || /* ResourceSpans */
		!walker_descend(buf.data, &pos, &end, 2) || /* ScopeSpans */
		!walker_descend(buf.data, &pos, &end, 2) || /* Span */
		!walker_descend(buf.data, &pos, &end, 9) || /* attributes */
		!walker_descend(buf.data, &pos, &end, 2) || /* value */
		!walker_descend(buf.data, &pos, &end, 6)) /* kvlist_value */
		goto out_buf;

	/* KeyValueList: first values{1} is an entry with key{1}="k1"
	 * and int_value{3}==v. */
	if (!walker_descend(buf.data, &pos, &end, 1))
		goto out_buf;
	if (!walker_find_at_level(buf.data, pos, end, 1, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_LEN || vl != 2 ||
		memcmp(buf.data + vp, "k1", 2) != 0)
		goto out_buf;
	if (!walker_descend(buf.data, &pos, &end, 2))
		goto out_buf;
	if (!walker_find_at_level(buf.data, pos, end, 3, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_VARINT)
		goto out_buf;
	{
		size_t pv = vp;
		uint64_t got = 0;

		if (decode_varint(buf.data, end, &pv, &got) != OTLP_OK ||
			got != v)
			goto out_buf;
	}
	ok = 1;

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_span_free(span);
	return ok;
}

/* ── main ─────────────────────────────────────────────────────── */

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_attr_string_roundtrip,
		"prop_attr_string_roundtrip",
		1000,
		1);
	failures += property_run(prop_attr_int64_roundtrip,
		"prop_attr_int64_roundtrip",
		1000,
		1);
	failures += property_run(prop_attr_double_roundtrip,
		"prop_attr_double_roundtrip",
		1000,
		1);
	failures += property_run(
		prop_attr_bool_roundtrip, "prop_attr_bool_roundtrip", 1000, 1);
	failures += property_run(prop_attr_bytes_roundtrip,
		"prop_attr_bytes_roundtrip",
		1000,
		1);
	failures += property_run(prop_attr_overflow_rejected,
		"prop_attr_overflow_rejected",
		1,
		1);
	failures += property_run(
		prop_attr_lookup_by_index, "prop_attr_lookup_by_index", 1, 1);
	failures += property_run(prop_attr_upsert_last_write_wins,
		"prop_attr_upsert_last_write_wins",
		1000,
		1);
	failures += property_run(
		prop_attr_upsert_at_cap, "prop_attr_upsert_at_cap", 1, 1);
	failures += property_run(prop_attr_upsert_wire_identical,
		"prop_attr_upsert_wire_identical",
		1000,
		1);
	failures += property_run(
		prop_attr_array_wire, "prop_attr_array_wire", 1000, 1);
	failures += property_run(
		prop_attr_kvlist_wire, "prop_attr_kvlist_wire", 1000, 1);

	if (failures)
		printf("[property] %d attr property(ies) failed\n", failures);
	else
		printf("[property] all attr properties passed\n");

	return failures ? 1 : 0;
}
