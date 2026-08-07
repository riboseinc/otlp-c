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

/* Attribute types map 1:1 to the AnyValue oneof field indices in
 * otlp_schema.h (OTLP_AV_FI_*). This alignment lets the encoder
 * look up the field spec via OTLP_AV_FIELDS[attr->type] without
 * a switch — OCP: adding a new type is a table entry, not a new
 * case statement. */
enum otlp_attr_type
{
	OTLP_ATTR_STRING	    = 0, /* OTLP_AV_FI_STRING  (field 1) */
	OTLP_ATTR_BOOL		    = 1, /* OTLP_AV_FI_BOOL    (field 2) */
	OTLP_ATTR_INT64	    = 2, /* OTLP_AV_FI_INT64   (field 3) */
	OTLP_ATTR_DOUBLE	    = 3, /* OTLP_AV_FI_DOUBLE  (field 4) */
	OTLP_ATTR_ARRAY	    = 4, /* OTLP_AV_FI_ARRAY_VALUE (field 5) */
	OTLP_ATTR_KVLIST	    = 5, /* OTLP_AV_FI_KVLIST_VALUE (field 6) */
	OTLP_ATTR_BYTES	    = 6, /* OTLP_AV_FI_BYTES   (field 7) */
};

/* ArrayValue: a list of AnyValue. Each item is an otlp_attribute
 * (key is unused / NULL inside an array — keys only exist on
 * KeyValue). Recursive: an array element can itself be array/kvlist.
 *
 * KeyValueList: a list of (key, AnyValue) pairs.
 *
 * Lifetime: array_val / kvlist_val are owned by the containing
 * attribute; freeing the attribute frees the whole tree via
 * otlp_attribute_free. */

/* Forward declarations: otlp_attribute contains pointers to array
 * and kvlist; array and kvlist contain otlp_attribute by value.
 * Define otlp_attribute first (with pointer members to incomplete
 * forward-declared types), then the contained types. */
struct otlp_attr_array;
struct otlp_attr_kvlist;

struct otlp_attribute
{
	char *key; /* owned, NULL-terminated; NULL when this attribute
		    * is an AnyValue inside an ArrayValue */
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
		struct otlp_attr_array  *array_val;  /* owned */
		struct otlp_attr_kvlist *kvlist_val; /* owned */
	} v;
};

struct otlp_attr_array
{
	struct otlp_attribute *items;  /* owned, count = n */
	size_t		       n;
};

struct otlp_attr_kvlist_entry
{
	char		       *key;   /* owned */
	struct otlp_attribute value;
};

struct otlp_attr_kvlist
{
	struct otlp_attr_kvlist_entry *entries;  /* owned, count = n */
	size_t				 n;
};

/* Recursively free an attribute's owned memory (key, value, and
 * any nested array/kvlist children). The attribute struct itself
 * is NOT freed (it may be embedded in an array). Safe with NULL. */
void otlp_attribute_free(struct otlp_attribute *a);

/* Span.Event — opentelemetry-proto Span.Event. v0.5 supports
 * name + time only; attributes are deferred (the API stubs accept
 * no attributes either). */
#define OTLP_EVENT_MAX_ATTRS  32
#define OTLP_LINK_MAX_ATTRS   32

struct otlp_event
{
	char	   *name;
	uint64_t    time_unix_nano;
	struct otlp_attribute attrs[OTLP_EVENT_MAX_ATTRS];
	size_t		n_attrs;
};

struct otlp_link
{
	uint8_t trace_id[OTLP_TRACE_ID_LEN];
	uint8_t span_id[OTLP_SPAN_ID_LEN];
	struct otlp_attribute attrs[OTLP_LINK_MAX_ATTRS];
	size_t			n_attrs;
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

/* Sampling flag (W3C trace-flags bit 0). Default: true. */
bool otlp_span_is_sampled(const otlp_span_t *span);

/* Returns a pointer to the internal attribute array and writes the
 * count to *n_out. The array is owned by the span. */
const struct otlp_attribute *
otlp_span_get_attrs(const otlp_span_t *span, size_t *n_out);

/* Events / links / trace_state accessors (v0.5+). */
const struct otlp_event *
otlp_span_get_events(const otlp_span_t *span, size_t *n_out);
const struct otlp_link *
otlp_span_get_links(const otlp_span_t *span, size_t *n_out);
const char *
otlp_span_get_trace_state(const otlp_span_t *span);

/* Deep-clone a span. Returns NULL on allocation failure. The caller
 * owns the result. Used by the exporter's emit() to honor the
 * "caller may free immediately" API contract. */
otlp_span_t *
otlp_span_clone(const otlp_span_t *src);

#endif
