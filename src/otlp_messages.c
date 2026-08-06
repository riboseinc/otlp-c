/* SPDX-License-Identifier: Apache-2.0 */
/*
 * OTLP message encoders. See src/otlp_messages.h for the design.
 *
 * Field numbers and wire types come from src/otlp_schema.h, which
 * is the model-driven single source of truth for the OTLP schema.
 * The encoders below are hand-rolled for clarity but reference
 * schema.h constants (O(N) lookups, but the compiler folds them
 * at -O2). Adding a new field is a one-line schema entry plus an
 * emit call.
 */
#include "otlp_messages.h"
#include "otlp_schema.h"
#include "protobuf_encode.h"
#include "span_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Field-number accessors — single source of truth is otlp_schema.h.
 * Each macro below extracts the number from the table entry; the
 * wire type is implied by the per-field emit. */
#define ETSR_F_RESOURCE_SPANS   OTLP_ETSR_FIELDS[0].number
#define RS_F_RESOURCE	    OTLP_RS_FIELDS[0].number
#define RS_F_SCOPE_SPANS	    OTLP_RS_FIELDS[1].number
#define R_F_ATTRIBUTES	    OTLP_R_FIELDS[0].number
#define SS_F_SCOPE		    OTLP_SS_FIELDS[0].number
#define SS_F_SPANS		    OTLP_SS_FIELDS[1].number
#define IS_F_NAME		    OTLP_IS_FIELDS[0].number
#define IS_F_VERSION		    OTLP_IS_FIELDS[1].number
#define SPAN_F_TRACE_ID	    OTLP_SPAN_FIELDS[0].number
#define SPAN_F_SPAN_ID	    OTLP_SPAN_FIELDS[1].number
#define SPAN_F_PARENT_SPAN_ID    OTLP_SPAN_FIELDS[3].number
#define SPAN_F_NAME		    OTLP_SPAN_FIELDS[4].number
#define SPAN_F_KIND		    OTLP_SPAN_FIELDS[5].number
#define SPAN_F_START_TIME	    OTLP_SPAN_FIELDS[6].number
#define SPAN_F_END_TIME	    OTLP_SPAN_FIELDS[7].number
#define SPAN_F_ATTRIBUTES	    OTLP_SPAN_FIELDS[8].number
#define SPAN_F_STATUS	    OTLP_SPAN_FIELDS[14].number
#define STATUS_F_CODE	    OTLP_STATUS_FIELDS[0].number
#define STATUS_F_MESSAGE	    OTLP_STATUS_FIELDS[1].number
#define KV_F_KEY		    OTLP_KV_FIELDS[0].number
#define KV_F_VALUE		    OTLP_KV_FIELDS[1].number
#define AV_F_STRING		    OTLP_AV_FIELDS[0].number
#define AV_F_BOOL		    OTLP_AV_FIELDS[1].number
#define AV_F_INT64		    OTLP_AV_FIELDS[2].number
#define AV_F_DOUBLE		    OTLP_AV_FIELDS[3].number
#define AV_F_BYTES		    OTLP_AV_FIELDS[6].number

/* ── AnyValue ───────────────────────────────────────────────────
 *
 * Always emit the oneof tag for the active variant, even when the
 * value is the type's zero (false, 0, 0.0, ""). The oneof indicator
 * is what tells the consumer which variant was chosen. Protobuf
 * "skip defaults" applies to ordinary fields, not to oneof selectors.
 */

static otlp_status_t
encode_any_value(struct otlp_pb_buf *buf, const struct otlp_attribute *a)
{
	otlp_status_t st;

	switch (a->type)
	{
		case OTLP_ATTR_STRING:
		{
			const char *s = a->v.string_val ? a->v.string_val : "";
			st = otlp_pb_tag(
				buf, AV_F_STRING, OTLP_PB_WIRE_LEN);
			if (st != OTLP_OK)
				return st;
			return otlp_pb_string(buf, s);
		}
		case OTLP_ATTR_BOOL:
		{
			st = otlp_pb_tag(
				buf, AV_F_BOOL, OTLP_PB_WIRE_VARINT);
			if (st != OTLP_OK)
				return st;
			return otlp_pb_varint(buf, a->v.bool_val ? 1ULL : 0ULL);
		}
		case OTLP_ATTR_INT64:
		{
			st = otlp_pb_tag(
				buf, AV_F_INT64, OTLP_PB_WIRE_VARINT);
			if (st != OTLP_OK)
				return st;
			return otlp_pb_varint(buf, (uint64_t) a->v.int64_val);
		}
		case OTLP_ATTR_DOUBLE:
		{
			uint64_t bits;

			memcpy(&bits, &a->v.double_val, sizeof(bits));
			st = otlp_pb_tag(
				buf, AV_F_DOUBLE, OTLP_PB_WIRE_FIXED64);
			if (st != OTLP_OK)
				return st;
			return otlp_pb_fixed64(buf, bits);
		}
		case OTLP_ATTR_BYTES:
		{
			const uint8_t *p = a->v.bytes_val.data;
			size_t len = a->v.bytes_val.len;
			st = otlp_pb_tag(buf, AV_F_BYTES, OTLP_PB_WIRE_LEN);
			if (st != OTLP_OK)
				return st;
			return otlp_pb_bytes(
				buf, p ? p : (const uint8_t *) "", len);
		}
	}
	return OTLP_ERR_INVALID_ARGUMENT;
}

/* ── KeyValue (public, for tests) ─────────────────────────────── */

otlp_status_t
otlp_encode_key_value(struct otlp_pb_buf *out,
	const char *key,
	const struct otlp_attribute *attr)
{
	struct otlp_pb_buf val_buf = { 0 };
	otlp_status_t st;

	if (!out || !attr)
		return OTLP_ERR_NULL;

	st = otlp_pb_buf_init(&val_buf, 0);
	if (st != OTLP_OK)
		return st;
	st = encode_any_value(&val_buf, attr);
	if (st != OTLP_OK)
		goto out;

	/* key (always emit, may be empty). */
	st = otlp_pb_tag(out, KV_F_KEY, OTLP_PB_WIRE_LEN);
	if (st != OTLP_OK)
		goto out;
	st = otlp_pb_string(out, key ? key : "");
	if (st != OTLP_OK)
		goto out;

	/* value (sub-message; even an empty AnyValue is emitted). */
	st = otlp_pb_field_message(
		out, KV_F_VALUE, val_buf.data, val_buf.len);

out:
	otlp_pb_buf_free(&val_buf);
	return st;
}

/* ── Status (private) ─────────────────────────────────────────── */

static otlp_status_t
emit_status(struct otlp_pb_buf *parent,
	uint32_t field_num,
	otlp_status_code_t code,
	const char *message)
{
	struct otlp_pb_buf sub = { 0 };
	otlp_status_t st;

	/* UNSET (0) is the default; omit the entire Status sub-message. */
	if (code == OTLP_STATUS_CODE_UNSET && !(message && message[0]))
		return OTLP_OK;

	st = otlp_pb_buf_init(&sub, 0);
	if (st != OTLP_OK)
		return st;

	if (code != OTLP_STATUS_CODE_UNSET)
	{
		st = otlp_pb_tag(&sub, STATUS_F_CODE, OTLP_PB_WIRE_VARINT);
		if (st != OTLP_OK)
			goto out;
		st = otlp_pb_varint(&sub, (uint64_t) code);
		if (st != OTLP_OK)
			goto out;
	}
	if (message && message[0])
	{
		st = otlp_pb_tag(&sub, STATUS_F_MESSAGE, OTLP_PB_WIRE_LEN);
		if (st != OTLP_OK)
			goto out;
		st = otlp_pb_string(&sub, message);
		if (st != OTLP_OK)
			goto out;
	}

	st = otlp_pb_field_message(parent, field_num, sub.data, sub.len);

out:
	otlp_pb_buf_free(&sub);
	return st;
}

/* ── Span body (public, for tests) ────────────────────────────── */

otlp_status_t
otlp_encode_span_body(struct otlp_pb_buf *out, const otlp_span_t *span)
{
	const uint8_t *trace_id;
	const uint8_t *span_id;
	const char *name;
	otlp_span_kind_t kind;
	size_t n_attrs;
	const struct otlp_attribute *attrs;
	size_t i;
	otlp_status_t st;

	if (!out || !span)
		return OTLP_ERR_NULL;

	trace_id = otlp_span_get_trace_id(span);
	span_id = otlp_span_get_span_id(span);

	/* trace_id (field 1) — always emit (required by spec). */
	st = otlp_pb_tag(out, SPAN_F_TRACE_ID, OTLP_PB_WIRE_LEN);
	if (st != OTLP_OK)
		return st;
	st = otlp_pb_bytes(out, trace_id, OTLP_TRACE_ID_LEN);
	if (st != OTLP_OK)
		return st;

	/* span_id (field 2) — always emit. */
	st = otlp_pb_tag(out, SPAN_F_SPAN_ID, OTLP_PB_WIRE_LEN);
	if (st != OTLP_OK)
		return st;
	st = otlp_pb_bytes(out, span_id, OTLP_SPAN_ID_LEN);
	if (st != OTLP_OK)
		return st;

	/* parent_span_id (field 4) — only if has_parent. */
	if (otlp_span_has_parent(span))
	{
		st = otlp_pb_tag(
			out, SPAN_F_PARENT_SPAN_ID, OTLP_PB_WIRE_LEN);
		if (st != OTLP_OK)
			return st;
		st = otlp_pb_bytes(out,
			otlp_span_get_parent_span_id(span),
			OTLP_SPAN_ID_LEN);
		if (st != OTLP_OK)
			return st;
	}

	/* name (field 5) — skip if empty. */
	name = otlp_span_get_name(span);
	if (name && name[0])
	{
		st = otlp_pb_field_string(out, SPAN_F_NAME, name);
		if (st != OTLP_OK)
			return st;
	}

	/* kind (field 6) — skip if UNSPECIFIED (protobuf default). */
	kind = otlp_span_get_kind(span);
	if (kind != OTLP_SPAN_KIND_UNSPECIFIED)
	{
		st = otlp_pb_field_varint(
			out, SPAN_F_KIND, (uint64_t) kind);
		if (st != OTLP_OK)
			return st;
	}

	/* start_time (field 7) — emit unconditionally if set. */
	if (otlp_span_has_start_time(span))
	{
		uint64_t t = otlp_span_get_start_time(span);
		st = otlp_pb_tag(
			out, SPAN_F_START_TIME, OTLP_PB_WIRE_FIXED64);
		if (st != OTLP_OK)
			return st;
		st = otlp_pb_fixed64(out, t);
		if (st != OTLP_OK)
			return st;
	}

	/* end_time (field 8). */
	if (otlp_span_has_end_time(span))
	{
		uint64_t t = otlp_span_get_end_time(span);
		st = otlp_pb_tag(
			out, SPAN_F_END_TIME, OTLP_PB_WIRE_FIXED64);
		if (st != OTLP_OK)
			return st;
		st = otlp_pb_fixed64(out, t);
		if (st != OTLP_OK)
			return st;
	}

	/* attributes (field 9, repeated). */
	attrs = otlp_span_get_attrs(span, &n_attrs);
	for (i = 0; i < n_attrs; i++)
	{
		struct otlp_pb_buf kv = { 0 };
		st = otlp_pb_buf_init(&kv, 0);
		if (st != OTLP_OK)
			return st;
		st = otlp_encode_key_value(&kv, attrs[i].key, &attrs[i]);
		if (st == OTLP_OK)
			st = otlp_pb_field_message(
				out, SPAN_F_ATTRIBUTES, kv.data, kv.len);
		otlp_pb_buf_free(&kv);
		if (st != OTLP_OK)
			return st;
	}

	/* status (field 15) — omitted for UNSET. */
	st = emit_status(out,
		SPAN_F_STATUS,
		otlp_span_get_status_code(span),
		otlp_span_get_status_message(span));
	if (st != OTLP_OK)
		return st;

	/* flags (field 16, fixed32) — W3C trace-flags. Emit when sampled
	 * so the wire value is 0x01 (the protobuf3 default 0x00 means
	 * "not sampled", so omission suffices for unsampled spans). */
	if (otlp_span_is_sampled(span)) {
		st = otlp_pb_tag(out, OTLP_SPAN_FIELDS[15].number,
				 OTLP_PB_WIRE_FIXED32);
		if (st != OTLP_OK)
			return st;
		st = otlp_pb_fixed32(out, 0x01);
		if (st != OTLP_OK)
			return st;
	}
	return OTLP_OK;
}

/* ── Resource / InstrumentationScope / ScopeSpans / ResourceSpans ─ */

static otlp_status_t
emit_resource(struct otlp_pb_buf *parent,
	uint32_t field_num,
	const char *service_name)
{
	struct otlp_pb_buf sub = { 0 };
	otlp_status_t st;

	if (!service_name || !service_name[0])
		return OTLP_OK;

	st = otlp_pb_buf_init(&sub, 0);
	if (st != OTLP_OK)
		return st;

	{
		struct otlp_attribute svc_attr = {
			.key = NULL,
			.type = OTLP_ATTR_STRING,
			.v.string_val = (char *) service_name,
		};
		struct otlp_pb_buf kv = { 0 };
		st = otlp_pb_buf_init(&kv, 0);
		if (st != OTLP_OK)
			goto out;
		st = otlp_encode_key_value(&kv, "service.name", &svc_attr);
		if (st == OTLP_OK)
			st = otlp_pb_field_message(
				&sub, R_F_ATTRIBUTES, kv.data, kv.len);
		otlp_pb_buf_free(&kv);
		if (st != OTLP_OK)
			goto out;
	}

	st = otlp_pb_field_message(parent, field_num, sub.data, sub.len);

out:
	otlp_pb_buf_free(&sub);
	return st;
}

static otlp_status_t
emit_instrumentation_scope(struct otlp_pb_buf *parent,
	uint32_t field_num,
	const char *name,
	const char *version)
{
	struct otlp_pb_buf sub = { 0 };
	otlp_status_t st;

	if (!(name && name[0]) && !(version && version[0]))
		return OTLP_OK;

	st = otlp_pb_buf_init(&sub, 0);
	if (st != OTLP_OK)
		return st;

	if (name && name[0])
	{
		st = otlp_pb_field_string(&sub, IS_F_NAME, name);
		if (st != OTLP_OK)
			goto out;
	}
	if (version && version[0])
	{
		st = otlp_pb_field_string(&sub, IS_F_VERSION, version);
		if (st != OTLP_OK)
			goto out;
	}

	st = otlp_pb_field_message(parent, field_num, sub.data, sub.len);

out:
	otlp_pb_buf_free(&sub);
	return st;
}

static otlp_status_t
emit_scope_spans(struct otlp_pb_buf *parent,
	uint32_t field_num,
	const char *scope_name,
	const char *scope_version,
	const otlp_span_t *const *spans,
	size_t n_spans)
{
	struct otlp_pb_buf sub = { 0 };
	otlp_status_t st;
	size_t i;

	st = otlp_pb_buf_init(&sub, 0);
	if (st != OTLP_OK)
		return st;

	st = emit_instrumentation_scope(
		&sub, SS_F_SCOPE, scope_name, scope_version);
	if (st != OTLP_OK)
		goto out;

	for (i = 0; i < n_spans; i++)
	{
		struct otlp_pb_buf sp = { 0 };
		st = otlp_pb_buf_init(&sp, 0);
		if (st != OTLP_OK)
			goto out;
		st = otlp_encode_span_body(&sp, spans[i]);
		if (st == OTLP_OK)
			st = otlp_pb_field_message(
				&sub, SS_F_SPANS, sp.data, sp.len);
		otlp_pb_buf_free(&sp);
		if (st != OTLP_OK)
			goto out;
	}

	if (sub.len > 0)
		st = otlp_pb_field_message(
			parent, field_num, sub.data, sub.len);

out:
	otlp_pb_buf_free(&sub);
	return st;
}

static otlp_status_t
emit_resource_spans(struct otlp_pb_buf *parent,
	uint32_t field_num,
	const char *service_name,
	const char *scope_name,
	const char *scope_version,
	const otlp_span_t *const *spans,
	size_t n_spans)
{
	struct otlp_pb_buf sub = { 0 };
	otlp_status_t st;

	st = otlp_pb_buf_init(&sub, 0);
	if (st != OTLP_OK)
		return st;

	st = emit_resource(&sub, RS_F_RESOURCE, service_name);
	if (st != OTLP_OK)
		goto out;

	st = emit_scope_spans(&sub,
		RS_F_SCOPE_SPANS,
		scope_name,
		scope_version,
		spans,
		n_spans);
	if (st != OTLP_OK)
		goto out;

	if (sub.len > 0)
		st = otlp_pb_field_message(
			parent, field_num, sub.data, sub.len);

out:
	otlp_pb_buf_free(&sub);
	return st;
}

/* ── Top-level encoder ────────────────────────────────────────── */

otlp_status_t
otlp_encode_export_trace_service_request(struct otlp_pb_buf *out,
	const char *service_name,
	const char *scope_name,
	const char *scope_version,
	const otlp_span_t *const *spans,
	size_t n_spans)
{
	if (!out)
		return OTLP_ERR_NULL;

	/* Empty request: zero spans and no service name → zero bytes. */
	if (n_spans == 0 && !(service_name && service_name[0]))
		return OTLP_OK;

	return emit_resource_spans(out,
		ETSR_F_RESOURCE_SPANS,
		service_name,
		scope_name,
		scope_version,
		spans,
		n_spans);
}
