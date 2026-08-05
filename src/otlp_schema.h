/* SPDX-License-Identifier: Apache-2.0 */
/*
 * OTLP schema tables — model-driven field-number / wire-type matrix
 * per OTLP message. Single source of truth; the encoders in
 * otlp_messages.c reference these constants instead of local #defines.
 *
 * Source: docs/otlp-spec.md (which mirrors opentelemetry-proto).
 */
#ifndef OTLP_C_OTLP_SCHEMA_H
#define OTLP_C_OTLP_SCHEMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protobuf_encode.h"

/* Wire types are reused from protobuf_encode.h:
 *   OTLP_PB_WIRE_VARINT, OTLP_PB_WIRE_FIXED64, OTLP_PB_WIRE_LEN,
 *   OTLP_PB_WIRE_FIXED32.
 */

/* Field presence semantics: protobuf3 default-omission vs explicit
 * emit (used for AnyValue oneof selectors). */
enum otlp_field_presence {
	OTLP_PRESENCE_DEFAULT_OMITTED = 0,  /* skip if zero value */
	OTLP_PRESENCE_ALWAYS_EMIT,	    /* emit even if zero (oneof selector) */
};

struct otlp_field_spec {
	const char	      *name;
	uint32_t	       number;
	int		       wire_type;
	enum otlp_field_presence presence;
	bool		       repeated;
};

/* ExportTraceServiceRequest */
static const struct otlp_field_spec OTLP_ETSR_FIELDS[] = {
	{"resource_spans", 1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, true},
};
static const size_t OTLP_ETSR_FIELDS_N =
    sizeof(OTLP_ETSR_FIELDS) / sizeof(OTLP_ETSR_FIELDS[0]);

/* ResourceSpans */
static const struct otlp_field_spec OTLP_RS_FIELDS[] = {
	{"resource",	     1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	{"scope_spans",     2, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, true},
	{"schema_url",	     3, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false}, /* deferred */
};
static const size_t OTLP_RS_FIELDS_N =
    sizeof(OTLP_RS_FIELDS) / sizeof(OTLP_RS_FIELDS[0]);

/* Resource */
static const struct otlp_field_spec OTLP_R_FIELDS[] = {
	{"attributes",		     1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, true},
	{"dropped_attributes_count", 2, OTLP_PB_WIRE_VARINT, OTLP_PRESENCE_DEFAULT_OMITTED, false}, /* deferred */
};
static const size_t OTLP_R_FIELDS_N =
    sizeof(OTLP_R_FIELDS) / sizeof(OTLP_R_FIELDS[0]);

/* ScopeSpans */
static const struct otlp_field_spec OTLP_SS_FIELDS[] = {
	{"scope",	     1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	{"spans",	     2, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, true},
	{"schema_url",	     3, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false}, /* deferred */
};
static const size_t OTLP_SS_FIELDS_N =
    sizeof(OTLP_SS_FIELDS) / sizeof(OTLP_SS_FIELDS[0]);

/* InstrumentationScope */
static const struct otlp_field_spec OTLP_IS_FIELDS[] = {
	{"name",		     1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	{"version",		     2, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	{"attributes",		     3, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_DEFAULT_OMITTED, true}, /* deferred */
	{"dropped_attributes_count", 4, OTLP_PB_WIRE_VARINT, OTLP_PRESENCE_DEFAULT_OMITTED, false}, /* deferred */
};
static const size_t OTLP_IS_FIELDS_N =
    sizeof(OTLP_IS_FIELDS) / sizeof(OTLP_IS_FIELDS[0]);

/* Span */
static const struct otlp_field_spec OTLP_SPAN_FIELDS[] = {
	{"trace_id",		     1,  OTLP_PB_WIRE_LEN,	  OTLP_PRESENCE_ALWAYS_EMIT, false},
	{"span_id",		     2,  OTLP_PB_WIRE_LEN,	  OTLP_PRESENCE_ALWAYS_EMIT, false},
	{"trace_state",		     3,  OTLP_PB_WIRE_LEN,	  OTLP_PRESENCE_DEFAULT_OMITTED, false}, /* deferred */
	{"parent_span_id",	     4,  OTLP_PB_WIRE_LEN,	  OTLP_PRESENCE_DEFAULT_OMITTED, false},
	{"name",		     5,  OTLP_PB_WIRE_LEN,	  OTLP_PRESENCE_DEFAULT_OMITTED, false},
	{"kind",		     6,  OTLP_PB_WIRE_VARINT,	  OTLP_PRESENCE_DEFAULT_OMITTED, false},
	{"start_time_unix_nano",     7,  OTLP_PB_WIRE_FIXED64, OTLP_PRESENCE_ALWAYS_EMIT, false},
	{"end_time_unix_nano",	     8,  OTLP_PB_WIRE_FIXED64, OTLP_PRESENCE_ALWAYS_EMIT, false},
	{"attributes",		     9,  OTLP_PB_WIRE_LEN,	  OTLP_PRESENCE_DEFAULT_OMITTED, true},
	{"dropped_attributes_count", 10, OTLP_PB_WIRE_VARINT,  OTLP_PRESENCE_DEFAULT_OMITTED, false}, /* deferred */
	{"events",		     11, OTLP_PB_WIRE_LEN,	  OTLP_PRESENCE_DEFAULT_OMITTED, true}, /* deferred */
	{"dropped_events_count",     12, OTLP_PB_WIRE_VARINT,  OTLP_PRESENCE_DEFAULT_OMITTED, false}, /* deferred */
	{"links",		     13, OTLP_PB_WIRE_LEN,	  OTLP_PRESENCE_DEFAULT_OMITTED, true}, /* deferred */
	{"dropped_links_count",	     14, OTLP_PB_WIRE_VARINT,  OTLP_PRESENCE_DEFAULT_OMITTED, false}, /* deferred */
	{"status",		     15, OTLP_PB_WIRE_LEN,	  OTLP_PRESENCE_DEFAULT_OMITTED, false},
	{"flags",		     16, OTLP_PB_WIRE_FIXED32, OTLP_PRESENCE_DEFAULT_OMITTED, false}, /* deferred */
};
static const size_t OTLP_SPAN_FIELDS_N =
    sizeof(OTLP_SPAN_FIELDS) / sizeof(OTLP_SPAN_FIELDS[0]);

/* Status */
static const struct otlp_field_spec OTLP_STATUS_FIELDS[] = {
	{"code",    1, OTLP_PB_WIRE_VARINT, OTLP_PRESENCE_DEFAULT_OMITTED, false},
	{"message", 2, OTLP_PB_WIRE_LEN,    OTLP_PRESENCE_DEFAULT_OMITTED, false},
};
static const size_t OTLP_STATUS_FIELDS_N =
    sizeof(OTLP_STATUS_FIELDS) / sizeof(OTLP_STATUS_FIELDS[0]);

/* KeyValue */
static const struct otlp_field_spec OTLP_KV_FIELDS[] = {
	{"key",   1, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_ALWAYS_EMIT, false},
	{"value", 2, OTLP_PB_WIRE_LEN, OTLP_PRESENCE_ALWAYS_EMIT, false},
};
static const size_t OTLP_KV_FIELDS_N =
    sizeof(OTLP_KV_FIELDS) / sizeof(OTLP_KV_FIELDS[0]);

/* AnyValue oneof variants. */
static const struct otlp_field_spec OTLP_AV_FIELDS[] = {
	{"string_value",   1, OTLP_PB_WIRE_LEN,	    OTLP_PRESENCE_ALWAYS_EMIT, false},
	{"bool_value",     2, OTLP_PB_WIRE_VARINT,   OTLP_PRESENCE_ALWAYS_EMIT, false},
	{"int_value",      3, OTLP_PB_WIRE_VARINT,   OTLP_PRESENCE_ALWAYS_EMIT, false},
	{"double_value",   4, OTLP_PB_WIRE_FIXED64,  OTLP_PRESENCE_ALWAYS_EMIT, false},
	{"array_value",    5, OTLP_PB_WIRE_LEN,	    OTLP_PRESENCE_ALWAYS_EMIT, false}, /* deferred */
	{"kvlist_value",   6, OTLP_PB_WIRE_LEN,	    OTLP_PRESENCE_ALWAYS_EMIT, false}, /* deferred */
	{"bytes_value",    7, OTLP_PB_WIRE_LEN,	    OTLP_PRESENCE_ALWAYS_EMIT, false},
};
static const size_t OTLP_AV_FIELDS_N =
    sizeof(OTLP_AV_FIELDS) / sizeof(OTLP_AV_FIELDS[0]);

#endif
