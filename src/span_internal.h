/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Span internals — internal-only header. Exposes the attribute type
 * and read-only accessors so the OTLP encoder (src/otlp_messages.c)
 * and tests can read a span's fields without owning its layout.
 *
 * NOT installed. NOT for caller code. The public span API remains
 * the surface in include/otlp-c/span.h.
 *
 * The struct otlp_span definition stays in span.c (opaque to all
 * consumers of this header). This preserves MECE: span.c owns the
 * layout, this header describes how to read it.
 */
#ifndef OTLP_C_SPAN_INTERNAL_H
#define OTLP_C_SPAN_INTERNAL_H

#include <otlp-c/span.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Attribute types map 1:1 to the OTLP AnyValue oneof variants we
 * support in v0.1.0. ArrayValue and KeyValueList are post-1.0. */
enum otlp_attr_type
{
	OTLP_ATTR_STRING,
	OTLP_ATTR_INT64,
	OTLP_ATTR_DOUBLE,
	OTLP_ATTR_BOOL,
	OTLP_ATTR_BYTES,
};

struct otlp_attribute
{
	char *key; /* owned, NULL-terminated */
	enum otlp_attr_type type;
	union
	{
		char *string_val; /* owned */
		int64_t int64_val;
		double double_val;
		bool bool_val;
		struct
		{
			uint8_t *data; /* owned */
			size_t len;
		} bytes_val;
	} v;
};

/* ── Read-only accessors for the encoder ────────────────────────
 *
 * All pointers returned point into the span; the caller must not
 * free them. They remain valid until otlp_span_free().
 */

const char *
otlp_span_get_name(const otlp_span_t *span);
const uint8_t *
otlp_span_get_trace_id(const otlp_span_t *span);
const uint8_t *
otlp_span_get_span_id(const otlp_span_t *span);
bool
otlp_span_has_parent(const otlp_span_t *span);
const uint8_t *
otlp_span_get_parent_span_id(const otlp_span_t *span);
otlp_span_kind_t
otlp_span_get_kind(const otlp_span_t *span);
bool
otlp_span_has_start_time(const otlp_span_t *span);
uint64_t
otlp_span_get_start_time(const otlp_span_t *span);
bool
otlp_span_has_end_time(const otlp_span_t *span);
uint64_t
otlp_span_get_end_time(const otlp_span_t *span);
otlp_status_code_t
otlp_span_get_status_code(const otlp_span_t *span);
const char *
otlp_span_get_status_message(const otlp_span_t *span);

/* Returns a pointer to the internal attribute array and writes the
 * count to *n_out. The array is owned by the span. */
const struct otlp_attribute *
otlp_span_get_attrs(const otlp_span_t *span, size_t *n_out);

/* Deep-clone a span. Returns NULL on allocation failure. The caller
 * owns the result. Used by the exporter's emit() to honor the
 * "caller may free immediately" API contract. */
otlp_span_t *
otlp_span_clone(const otlp_span_t *src);

#endif
