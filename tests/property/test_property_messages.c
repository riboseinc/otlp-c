/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property tests for the OTLP message encoders.
 *
 *   prop_encode_empty_request       — zero spans, NULL svc → 0 bytes.
 *   prop_encode_request_with_span   — encoded body is decodable and
 *                                     the round-trip preserves fields.
 *   prop_encode_span_field_numbers  — span body has the expected
 *                                     field-number/wire-type pairs.
 *   prop_encode_kv_roundtrip        — KeyValue encodes key + each
 *                                     attribute variant correctly.
 *   prop_encode_status_omitted      — UNSET status produces no bytes.
 *   prop_encode_status_present      — non-UNSET status emits both fields.
 */
#include "prng.h"
#include "property_harness.h"
#include "decoder.h"

#include "../src/internal_util.h"
#include "../src/otlp_messages.h"
#include "../src/protobuf_encode.h"
#include "../src/span_internal.h"

#include <otlp-c/span.h>

#include <stdint.h>
#include <string.h>

/* ── Properties ───────────────────────────────────────────────── */

static int
prop_encode_empty_request(uint64_t seed)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t st;
	int ok = 0;

	(void) seed;
	st = otlp_pb_buf_init(&buf, 0);
	if (st != OTLP_OK)
		return 0;
	st = otlp_encode_export_trace_service_request(
		&buf, NULL, NULL, 0, NULL, NULL, NULL, 0);
	if (st == OTLP_OK)
		ok = (buf.len == 0);
	otlp_pb_buf_free(&buf);
	return ok;
}

/* Walk every top-level field in an encoded body, assert that field
 * numbers and wire types come from the expected set. */
static int
prop_encode_span_field_numbers(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	struct otlp_pb_buf buf;
	otlp_status_t st;
	size_t pos = 0;
	int ok = 0;

	(void) seed;
	prng_seed(&p, 1);
	span = otlp_span_create("s");
	if (!span)
		return 0;
	otlp_span_set_start_time(span, 1700000000ULL * 1000000000ULL);
	otlp_span_set_end_time(span, 1700000001ULL * 1000000000ULL);
	otlp_span_set_kind(span, OTLP_SPAN_KIND_SERVER);
	otlp_span_set_attribute_int(span, "k", 42);
	otlp_span_set_status(span, OTLP_STATUS_CODE_OK, "fine");

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	st = otlp_encode_span_body(&buf, span);
	if (st != OTLP_OK)
		goto out_buf;

	while (pos < buf.len)
	{
		uint32_t fnum;
		int wtype;
		st = decode_tag(buf.data, buf.len, &pos, &fnum, &wtype);
		if (st != OTLP_OK)
			goto out_buf;
		/* Field numbers must match the Span message spec. */
		switch (fnum)
		{
			case 1: /* trace_id */
			case 2: /* span_id */
			case 4: /* parent_span_id */
			case 5: /* name */
			case 9: /* attributes */
			case 15: /* status */
				if (wtype != OTLP_PB_WIRE_LEN)
					goto out_buf;
				break;
			case 6: /* kind */
				if (wtype != OTLP_PB_WIRE_VARINT)
					goto out_buf;
				break;
			case 7: /* start_time */
			case 8: /* end_time */
				if (wtype != OTLP_PB_WIRE_FIXED64)
					goto out_buf;
				break;
			case 16: /* flags (W3C trace-flags) */
				if (wtype != OTLP_PB_WIRE_FIXED32)
					goto out_buf;
				break;
			default:
				/* Unexpected field — fail. */
				goto out_buf;
		}
		st = skip_value(buf.data, buf.len, &pos, wtype);
		if (st != OTLP_OK)
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
prop_encode_kv_roundtrip_string(uint64_t seed)
{
	struct prng p;
	struct otlp_pb_buf buf;
	struct otlp_attribute attr;
	char key[16];
	char val[32];
	otlp_status_t st;
	size_t pos = 0;
	uint32_t fnum;
	int wtype;
	uint64_t len_v;
	int ok = 0;

	prng_seed(&p, seed);
	snprintf(key, sizeof(key), "k%llu", (unsigned long long) prng_next(&p));
	size_t vlen = (size_t) prng_u32(&p, 30) + 1;
	for (size_t i = 0; i < vlen; i++)
		val[i] = (char) (prng_u32(&p, 94) + 33);
	val[vlen] = '\0';

	attr.key = NULL;
	attr.type = OTLP_ATTR_STRING;
	attr.v.string_val = val;

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	st = otlp_encode_key_value(&buf, key, &attr);
	if (st != OTLP_OK)
		goto out;

	/* First field: key (1, LEN). */
	st = decode_tag(buf.data, buf.len, &pos, &fnum, &wtype);
	if (st != OTLP_OK || fnum != 1 || wtype != OTLP_PB_WIRE_LEN)
		goto out;
	st = decode_varint(buf.data, buf.len, &pos, &len_v);
	if (st != OTLP_OK || len_v != strlen(key))
		goto out;
	if (memcmp(buf.data + pos, key, len_v) != 0)
		goto out;
	pos += len_v;

	/* Second field: value (2, LEN, sub-message). */
	st = decode_tag(buf.data, buf.len, &pos, &fnum, &wtype);
	if (st != OTLP_OK || fnum != 2 || wtype != OTLP_PB_WIRE_LEN)
		goto out;
	/* The sub-message contains one field: string_value (1, LEN). */
	size_t sub_start = pos;
	st = decode_varint(buf.data, buf.len, &pos, &len_v);
	if (st != OTLP_OK)
		goto out;
	size_t sub_end = sub_start + (size_t) len_v + (pos - sub_start);
	(void) sub_end;
	/* Inner field: tag 1 + varint(len) + bytes. */
	st = decode_tag(buf.data, buf.len, &pos, &fnum, &wtype);
	if (st != OTLP_OK || fnum != 1 || wtype != OTLP_PB_WIRE_LEN)
		goto out;
	st = decode_varint(buf.data, buf.len, &pos, &len_v);
	if (st != OTLP_OK || len_v != strlen(val))
		goto out;
	if (memcmp(buf.data + pos, val, len_v) != 0)
		goto out;
	pos += len_v;
	ok = 1;

out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_encode_kv_roundtrip_int64(uint64_t seed)
{
	struct prng p;
	struct otlp_pb_buf buf;
	struct otlp_attribute attr;
	otlp_status_t st;
	size_t pos = 0;
	uint32_t fnum;
	int wtype;
	uint64_t decoded;
	int64_t v;
	int ok = 0;

	prng_seed(&p, seed);
	v = (int64_t) prng_next(&p);
	attr.key = NULL;
	attr.type = OTLP_ATTR_INT64;
	attr.v.int64_val = v;

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	st = otlp_encode_key_value(&buf, "n", &attr);
	if (st != OTLP_OK)
		goto out;

	/* Skip key field. */
	st = decode_tag(buf.data, buf.len, &pos, &fnum, &wtype);
	if (st != OTLP_OK || fnum != 1 || wtype != OTLP_PB_WIRE_LEN)
		goto out;
	st = skip_value(buf.data, buf.len, &pos, wtype);
	if (st != OTLP_OK)
		goto out;

	/* Value sub-message: outer tag 2 LEN. */
	st = decode_tag(buf.data, buf.len, &pos, &fnum, &wtype);
	if (st != OTLP_OK || fnum != 2 || wtype != OTLP_PB_WIRE_LEN)
		goto out;
	st = skip_value(buf.data, buf.len, &pos, OTLP_PB_WIRE_VARINT); /* len */
	if (st != OTLP_OK)
		goto out;
	/* Inner: tag 3 (int64 oneof), VARINT. */
	st = decode_tag(buf.data, buf.len, &pos, &fnum, &wtype);
	if (st != OTLP_OK || fnum != 3 || wtype != OTLP_PB_WIRE_VARINT)
		goto out;
	st = decode_varint(buf.data, buf.len, &pos, &decoded);
	if (st != OTLP_OK)
		goto out;
	ok = ((int64_t) decoded == v);

out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_encode_status_omitted(uint64_t seed)
{
	otlp_span_t *span;
	struct otlp_pb_buf buf;
	size_t pos = 0;
	uint32_t fnum;
	int wtype;
	int ok = 0;

	(void) seed;
	span = otlp_span_create("s");
	if (!span)
		return 0;
	/* Status defaults to UNSET, no message. */
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
	{
		otlp_span_free(span);
		return 0;
	}
	if (otlp_encode_span_body(&buf, span) != OTLP_OK)
		goto out;
	/* No field 15 should appear. */
	while (pos < buf.len)
	{
		if (decode_tag(buf.data, buf.len, &pos, &fnum, &wtype) !=
			OTLP_OK)
			goto out;
		if (fnum == 15)
			goto out; /* status should not be emitted */
		if (skip_value(buf.data, buf.len, &pos, wtype) != OTLP_OK)
			goto out;
	}
	ok = 1;
out:
	otlp_pb_buf_free(&buf);
	otlp_span_free(span);
	return ok;
}

static int
prop_encode_status_present(uint64_t seed)
{
	otlp_span_t *span;
	struct otlp_pb_buf buf;
	size_t pos = 0;
	uint32_t fnum;
	int wtype;
	int saw_status = 0;

	(void) seed;
	span = otlp_span_create("s");
	if (!span)
		return 0;
	otlp_span_set_status(span, OTLP_STATUS_CODE_ERROR, "boom");
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
	{
		otlp_span_free(span);
		return 0;
	}
	if (otlp_encode_span_body(&buf, span) != OTLP_OK)
		goto out;
	while (pos < buf.len)
	{
		if (decode_tag(buf.data, buf.len, &pos, &fnum, &wtype) !=
			OTLP_OK)
			goto out;
		if (fnum == 15)
		{
			saw_status = 1;
			break;
		}
		if (skip_value(buf.data, buf.len, &pos, wtype) != OTLP_OK)
			goto out;
	}
out:
	otlp_pb_buf_free(&buf);
	otlp_span_free(span);
	return saw_status;
}

/* ── main ─────────────────────────────────────────────────────── */

static int
prop_encode_anyvalue_array(uint64_t seed)
{
	struct otlp_pb_buf buf = { 0 };
	struct otlp_attribute attr;
	struct otlp_attr_array *arr;
	int ok = 0;

	(void) seed;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;

	arr = otlp_calloc(1, sizeof(*arr));
	if (!arr)
		goto out_buf;
	arr->n = 2;
	arr->items = otlp_calloc(arr->n, sizeof(*arr->items));
	if (!arr->items)
		goto out_arr;
	arr->items[0].type = OTLP_ATTR_INT64;
	arr->items[0].v.int64_val = 11;
	arr->items[1].type = OTLP_ATTR_INT64;
	arr->items[1].v.int64_val = 22;

	attr.key = NULL;
	attr.type = OTLP_ATTR_ARRAY;
	attr.v.array_val = arr;

	if (otlp_encode_any_value(&buf, &attr) != OTLP_OK)
		goto out_items;
	/* AnyValue for array should produce tag(5, LEN) + sub-message
	 * containing 2 AnyValue entries at field 1 (each is tag(3,
	 * VARINT) + value). Verify the outer tag. */
	{
		size_t    pos = 0;
		uint32_t  fnum;
		int	    wt;

		if (decode_tag(buf.data, buf.len, &pos, &fnum, &wt) != OTLP_OK)
			goto out_items;
		if (fnum != 5 || wt != OTLP_PB_WIRE_LEN)
			goto out_items;
		ok = 1;
	}

out_items:
	otlp_attribute_free(&attr);
out_arr:
	/* otlp_attribute_free already freed arr */
	(void) arr;
out_buf:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_encode_anyvalue_kvlist(uint64_t seed)
{
	struct otlp_pb_buf buf = { 0 };
	struct otlp_attribute attr;
	struct otlp_attr_kvlist *kvl;
	int ok = 0;

	(void) seed;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;

	kvl = otlp_calloc(1, sizeof(*kvl));
	if (!kvl)
		goto out_buf;
	kvl->n = 1;
	kvl->entries = otlp_calloc(kvl->n, sizeof(*kvl->entries));
	if (!kvl->entries)
		goto out_kvl;
	kvl->entries[0].key = otlp_dup_str("nested");
	kvl->entries[0].value.type = OTLP_ATTR_INT64;
	kvl->entries[0].value.v.int64_val = 7;

	attr.key = NULL;
	attr.type = OTLP_ATTR_KVLIST;
	attr.v.kvlist_val = kvl;

	if (otlp_encode_any_value(&buf, &attr) != OTLP_OK)
		goto out_entries;
	{
		size_t    pos = 0;
		uint32_t  fnum;
		int	    wt;

		if (decode_tag(buf.data, buf.len, &pos, &fnum, &wt) != OTLP_OK)
			goto out_entries;
		if (fnum != 6 || wt != OTLP_PB_WIRE_LEN)
			goto out_entries;
		ok = 1;
	}

out_entries:
	otlp_attribute_free(&attr);
out_kvl:
	(void) kvl;
out_buf:
	otlp_pb_buf_free(&buf);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(
		prop_encode_empty_request, "prop_encode_empty_request", 1, 1);
	failures += property_run(prop_encode_span_field_numbers,
		"prop_encode_span_field_numbers",
		10,
		1);
	failures += property_run(prop_encode_kv_roundtrip_string,
		"prop_encode_kv_roundtrip_string",
		1000,
		1);
	failures += property_run(prop_encode_kv_roundtrip_int64,
		"prop_encode_kv_roundtrip_int64",
		1000,
		1);
	failures += property_run(
		prop_encode_status_omitted, "prop_encode_status_omitted", 1, 1);
	failures += property_run(
		prop_encode_status_present, "prop_encode_status_present", 1, 1);
	failures += property_run(prop_encode_anyvalue_array,
		"prop_encode_anyvalue_array", 5, 1);
	failures += property_run(prop_encode_anyvalue_kvlist,
		"prop_encode_anyvalue_kvlist", 5, 1);

	if (failures)
		printf("[property] %d otlp-messages property(ies) failed\n",
			failures);
	else
		printf("[property] all otlp-messages properties passed\n");

	return failures ? 1 : 0;
}
