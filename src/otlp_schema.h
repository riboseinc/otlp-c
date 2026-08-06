/* SPDX-License-Identifier: Apache-2.0 */
/*
 * OTLP schema tables — model-driven field-number / wire-type matrix
 * per OTLP message. Single source of truth; the encoders in
 * otlp_messages.c reference these constants instead of local #defines.
 *
 * Source: docs/otlp-spec.md (which mirrors opentelemetry-proto).
 *
 * Design: each message has a named-enum index + designated-initializer
 * table. Adding or reordering fields is safe — the enum ensures the
 * accessor macros (in otlp_messages.c) always reference the right
 * entry regardless of array order.
 */
#ifndef OTLP_C_OTLP_SCHEMA_H
#define OTLP_C_OTLP_SCHEMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protobuf_encode.h"

/* ── Types ────────────────────────────────────────────────────── */

enum otlp_field_presence {
	OTLP_PRESENCE_DEFAULT_OMITTED = 0,
	OTLP_PRESENCE_ALWAYS_EMIT,
};

struct otlp_field_spec {
	const char	      *name;
	uint32_t	       number;
	int		       wire_type;
	enum otlp_field_presence presence;
	bool		       repeated;
};

/* ── ExportTraceServiceRequest ────────────────────────────────── */

enum {
	OTLP_ETSR_FI_RESOURCE_SPANS,
	OTLP_ETSR_FI_COUNT,
};

static const struct otlp_field_spec OTLP_ETSR_FIELDS[] = {
	[OTLP_ETSR_FI_RESOURCE_SPANS] = {
		"resource_spans", 1, OTLP_PB_WIRE_LEN,
		OTLP_PRESENCE_DEFAULT_OMITTED, true
	},
};

/* ── ResourceSpans ────────────────────────────────────────────── */

enum {
	OTLP_RS_FI_RESOURCE,
	OTLP_RS_FI_SCOPE_SPANS,
	OTLP_RS_FI_SCHEMA_URL,
	OTLP_RS_FI_COUNT,
};

static const struct otlp_field_spec OTLP_RS_FIELDS[] = {
	[OTLP_RS_FI_RESOURCE]	    = {"resource", 1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_RS_FI_SCOPE_SPANS]    = {"scope_spans", 2, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, true},
	[OTLP_RS_FI_SCHEMA_URL]	    = {"schema_url", 3, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
};

/* ── Resource ─────────────────────────────────────────────────── */

enum {
	OTLP_R_FI_ATTRIBUTES,
	OTLP_R_FI_DROPPED_ATTRS,
	OTLP_R_FI_COUNT,
};

static const struct otlp_field_spec OTLP_R_FIELDS[] = {
	[OTLP_R_FI_ATTRIBUTES]	 = {"attributes", 1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, true},
	[OTLP_R_FI_DROPPED_ATTRS] = {"dropped_attributes_count", 2, OTLP_PB_WIRE_VARINT, OTLP_PRESENCE_DEFAULT_OMITTED, false},
};

/* ── ScopeSpans ───────────────────────────────────────────────── */

enum {
	OTLP_SS_FI_SCOPE,
	OTLP_SS_FI_SPANS,
	OTLP_SS_FI_SCHEMA_URL,
	OTLP_SS_FI_COUNT,
};

static const struct otlp_field_spec OTLP_SS_FIELDS[] = {
	[OTLP_SS_FI_SCOPE]	 = {"scope", 1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_SS_FI_SPANS]	 = {"spans", 2, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, true},
	[OTLP_SS_FI_SCHEMA_URL]	 = {"schema_url", 3, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
};

/* ── InstrumentationScope ─────────────────────────────────────── */

enum {
	OTLP_IS_FI_NAME,
	OTLP_IS_FI_VERSION,
	OTLP_IS_FI_ATTRIBUTES,
	OTLP_IS_FI_DROPPED_ATTRS,
	OTLP_IS_FI_COUNT,
};

static const struct otlp_field_spec OTLP_IS_FIELDS[] = {
	[OTLP_IS_FI_NAME]		 = {"name", 1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_IS_FI_VERSION]		 = {"version", 2, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_IS_FI_ATTRIBUTES]	 = {"attributes", 3, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, true},
	[OTLP_IS_FI_DROPPED_ATTRS]	 = {"dropped_attributes_count", 4, OTLP_PB_WIRE_VARINT, OTLP_PRESENCE_DEFAULT_OMITTED, false},
};

/* ── Span ─────────────────────────────────────────────────────── */

enum {
	OTLP_SPAN_FI_TRACE_ID,
	OTLP_SPAN_FI_SPAN_ID,
	OTLP_SPAN_FI_TRACE_STATE,
	OTLP_SPAN_FI_PARENT_SPAN_ID,
	OTLP_SPAN_FI_NAME,
	OTLP_SPAN_FI_KIND,
	OTLP_SPAN_FI_START_TIME,
	OTLP_SPAN_FI_END_TIME,
	OTLP_SPAN_FI_ATTRIBUTES,
	OTLP_SPAN_FI_DROPPED_ATTRS,
	OTLP_SPAN_FI_EVENTS,
	OTLP_SPAN_FI_DROPPED_EVENTS,
	OTLP_SPAN_FI_LINKS,
	OTLP_SPAN_FI_DROPPED_LINKS,
	OTLP_SPAN_FI_STATUS,
	OTLP_SPAN_FI_FLAGS,
	OTLP_SPAN_FI_COUNT,
};

static const struct otlp_field_spec OTLP_SPAN_FIELDS[] = {
	[OTLP_SPAN_FI_TRACE_ID]	 = {"trace_id", 1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_ALWAYS_EMIT, false},
	[OTLP_SPAN_FI_SPAN_ID]	 = {"span_id", 2, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_ALWAYS_EMIT, false},
	[OTLP_SPAN_FI_TRACE_STATE]	 = {"trace_state", 3, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_SPAN_FI_PARENT_SPAN_ID] = {"parent_span_id", 4, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_SPAN_FI_NAME]		 = {"name", 5, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_SPAN_FI_KIND]		 = {"kind", 6, OTLP_PB_WIRE_VARINT, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_SPAN_FI_START_TIME]	 = {"start_time_unix_nano", 7, OTLP_PB_WIRE_FIXED64, OTLP_PRESENCE_ALWAYS_EMIT, false},
	[OTLP_SPAN_FI_END_TIME]	 = {"end_time_unix_nano", 8, OTLP_PB_WIRE_FIXED64, OTLP_PRESENCE_ALWAYS_EMIT, false},
	[OTLP_SPAN_FI_ATTRIBUTES]	 = {"attributes", 9, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, true},
	[OTLP_SPAN_FI_DROPPED_ATTRS]	 = {"dropped_attributes_count", 10, OTLP_PB_WIRE_VARINT, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_SPAN_FI_EVENTS]		 = {"events", 11, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, true},
	[OTLP_SPAN_FI_DROPPED_EVENTS]	 = {"dropped_events_count", 12, OTLP_PB_WIRE_VARINT, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_SPAN_FI_LINKS]		 = {"links", 13, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, true},
	[OTLP_SPAN_FI_DROPPED_LINKS]	 = {"dropped_links_count", 14, OTLP_PB_WIRE_VARINT, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_SPAN_FI_STATUS]		 = {"status", 15, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_SPAN_FI_FLAGS]		 = {"flags", 16, OTLP_PB_WIRE_FIXED32, OTLP_PRESENCE_DEFAULT_OMITTED, false},
};

/* ── Status ───────────────────────────────────────────────────── */

enum {
	OTLP_STATUS_FI_CODE,
	OTLP_STATUS_FI_MESSAGE,
	OTLP_STATUS_FI_COUNT,
};

static const struct otlp_field_spec OTLP_STATUS_FIELDS[] = {
	[OTLP_STATUS_FI_CODE]	 = {"code", 1, OTLP_PB_WIRE_VARINT, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	[OTLP_STATUS_FI_MESSAGE]	 = {"message", 2, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
};

/* ── KeyValue ─────────────────────────────────────────────────── */

enum {
	OTLP_KV_FI_KEY,
	OTLP_KV_FI_VALUE,
	OTLP_KV_FI_COUNT,
};

static const struct otlp_field_spec OTLP_KV_FIELDS[] = {
	[OTLP_KV_FI_KEY]	 = {"key", 1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_ALWAYS_EMIT, false},
	[OTLP_KV_FI_VALUE]	 = {"value", 2, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_ALWAYS_EMIT, false},
};

/* ── AnyValue oneof variants ──────────────────────────────────── */

enum {
	OTLP_AV_FI_STRING,
	OTLP_AV_FI_BOOL,
	OTLP_AV_FI_INT64,
	OTLP_AV_FI_DOUBLE,
	OTLP_AV_FI_ARRAY_VALUE,
	OTLP_AV_FI_KVLIST_VALUE,
	OTLP_AV_FI_BYTES,
	OTLP_AV_FI_COUNT,
};

static const struct otlp_field_spec OTLP_AV_FIELDS[] = {
	[OTLP_AV_FI_STRING]	 = {"string_value", 1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_ALWAYS_EMIT, false},
	[OTLP_AV_FI_BOOL]	 = {"bool_value", 2, OTLP_PB_WIRE_VARINT, OTLP_PRESENCE_ALWAYS_EMIT, false},
	[OTLP_AV_FI_INT64]	 = {"int_value", 3, OTLP_PB_WIRE_VARINT, OTLP_PRESENCE_ALWAYS_EMIT, false},
	[OTLP_AV_FI_DOUBLE]	 = {"double_value", 4, OTLP_PB_WIRE_FIXED64, OTLP_PRESENCE_ALWAYS_EMIT, false},
	[OTLP_AV_FI_ARRAY_VALUE] = {"array_value", 5, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_ALWAYS_EMIT, false},
	[OTLP_AV_FI_KVLIST_VALUE] = {"kvlist_value", 6, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_ALWAYS_EMIT, false},
	[OTLP_AV_FI_BYTES]	 = {"bytes_value", 7, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_ALWAYS_EMIT, false},
};

#endif
