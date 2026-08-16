/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Span lifecycle and attribute storage.
 *
 * A span owns: its name, its status message, and any heap-allocated
 * attribute payload (strings, byte arrays). The fixed-size fields
 * (IDs, times, kind) are inline. Attributes are stored in a fixed-
 * cap inline array (default 128); overflow returns OTLP_ERR_OVERFLOW.
 *
 * Thread-safety: spans are single-threaded by API contract. The
 * caller builds, mutates, and frees a span on one thread (or
 * synchronizes ownership transfer explicitly). The exporter copies
 * spans into its queue; the original is then free to be freed.
 */
#include <otlp-c/span.h>

#include "internal_util.h"
#include "span_internal.h"
#include "platform.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef OTLP_SPAN_MAX_ATTRIBUTES
#define OTLP_SPAN_MAX_ATTRIBUTES 128
#endif
#ifndef OTLP_SPAN_MAX_EVENTS
#define OTLP_SPAN_MAX_EVENTS 64
#endif
#ifndef OTLP_SPAN_MAX_LINKS
#define OTLP_SPAN_MAX_LINKS 64
#endif

struct otlp_span
{
	char *name; /* owned */
	uint8_t trace_id[OTLP_TRACE_ID_LEN];
	uint8_t span_id[OTLP_SPAN_ID_LEN];
	uint8_t parent_span_id[OTLP_SPAN_ID_LEN];
	bool has_parent;
	uint64_t start_time_unix_nano;
	uint64_t end_time_unix_nano;
	bool has_start_time;
	bool has_end_time;
	otlp_span_kind_t kind;
	struct otlp_attribute attrs[OTLP_SPAN_MAX_ATTRIBUTES];
	size_t n_attrs;
	otlp_status_code_t status_code;
	char *status_message; /* owned, may be NULL */
	bool sampled;
	char *trace_state; /* owned, may be NULL */
	struct otlp_event events[OTLP_SPAN_MAX_EVENTS];
	size_t n_events;
	struct otlp_link links[OTLP_SPAN_MAX_LINKS];
	size_t n_links;
};

/* ── Internal helpers ─────────────────────────────────────────── */

/* Upsert-reserve the slot for `key` in the span's inline attribute
 * array (same semantics as otlp_attr_list_reserve — see
 * internal_util.h): reuse an existing key's slot, releasing its
 * old value, or append and bump n_attrs. Caller fills the typed
 * value, which must not fail. */
static otlp_status_t
attr_reserve(otlp_span_t *span, const char *key, struct otlp_attribute **out)
{
	struct otlp_attribute *a;
	char *key_copy;
	size_t idx;

	if (!span || !key)
		return OTLP_ERR_NULL;
	if (otlp_attr_list_find(span->attrs, span->n_attrs, key, &idx))
	{
		a = &span->attrs[idx];
		otlp_attribute_release_value(a);
		*out = a;
		return OTLP_OK;
	}
	if (span->n_attrs >= OTLP_SPAN_MAX_ATTRIBUTES)
		return OTLP_ERR_OVERFLOW;

	key_copy = otlp_dup_str(key);
	if (!key_copy)
		return OTLP_ERR_NOMEM;

	a = &span->attrs[span->n_attrs];
	a->key = key_copy;
	/* Zero the union so cleanup paths don't see garbage. */
	a->v.string_val = NULL;
	a->type = OTLP_ATTR_STRING; /* tentative; caller overrides */

	span->n_attrs++;
	*out = a;
	return OTLP_OK;
}

/* Release one attribute's owned fields. Does NOT touch the slot
 * itself (caller frees the array as a whole). */
static void
attr_release(struct otlp_attribute *a)
{
	if (!a)
		return;
	otlp_free(a->key);
	a->key = NULL;
	switch (a->type)
	{
		case OTLP_ATTR_STRING:
			otlp_free(a->v.string_val);
			a->v.string_val = NULL;
			break;
		case OTLP_ATTR_BYTES:
			otlp_free(a->v.bytes_val.data);
			a->v.bytes_val.data = NULL;
			a->v.bytes_val.len = 0;
			break;
		default:
			/* int64, double, bool: no owned memory. */
			break;
	}
}

static void
span_release_attrs(otlp_span_t *span)
{
	size_t i;

	if (!span)
		return;
	for (i = 0; i < span->n_attrs; i++)
		attr_release(&span->attrs[i]);
	span->n_attrs = 0;
}

/* ── Lifecycle ────────────────────────────────────────────────── */

otlp_span_t *
otlp_span_create(const char *name)
{
	otlp_span_t *span;
	char *name_copy;

	span = otlp_malloc(sizeof(*span));
	if (!span)
		return NULL;
	memset(span, 0, sizeof(*span));

	name_copy = otlp_dup_str(name ? name : "");
	if (!name_copy)
	{
		otlp_free(span);
		return NULL;
	}
	span->name = name_copy;
	span->kind = OTLP_SPAN_KIND_INTERNAL;
	span->status_code = OTLP_STATUS_CODE_UNSET;
	span->sampled = true;
	return span;
}

void
otlp_span_free(otlp_span_t *span)
{
	size_t i;

	if (!span)
		return;
	otlp_free(span->name);
	otlp_free(span->status_message);
	otlp_free(span->trace_state);
	for (i = 0; i < span->n_events; i++)
	{
		otlp_free(span->events[i].name);
		otlp_attr_list_free(
			&span->events[i].attrs, &span->events[i].n_attrs);
	}
	for (i = 0; i < span->n_links; i++)
		otlp_attr_list_free(
			&span->links[i].attrs, &span->links[i].n_attrs);
	span_release_attrs(span);
	otlp_free(span);
}

/* ── Identity ─────────────────────────────────────────────────── */

otlp_status_t
otlp_span_set_trace_id(otlp_span_t *span, const uint8_t *trace_id)
{
	if (!span)
		return OTLP_ERR_NULL;
	if (!trace_id)
		return OTLP_ERR_NULL;
	/* W3C Trace Context §3.1.1: trace-id MUST NOT be all-zero. */
	if (otlp_id_is_all_zero(trace_id, OTLP_TRACE_ID_LEN))
		return OTLP_ERR_INVALID_ARGUMENT;
	memcpy(span->trace_id, trace_id, OTLP_TRACE_ID_LEN);
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_span_id(otlp_span_t *span, const uint8_t *span_id)
{
	if (!span || !span_id)
		return OTLP_ERR_NULL;
	/* W3C Trace Context §3.1.2: parent-id (span-id) MUST NOT be
	 * all-zero. */
	if (otlp_id_is_all_zero(span_id, OTLP_SPAN_ID_LEN))
		return OTLP_ERR_INVALID_ARGUMENT;
	memcpy(span->span_id, span_id, OTLP_SPAN_ID_LEN);
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_parent_span_id(otlp_span_t *span, const uint8_t *parent)
{
	if (!span)
		return OTLP_ERR_NULL;
	/* parent == NULL clears the parent link. */
	if (!parent)
	{
		memset(span->parent_span_id, 0, OTLP_SPAN_ID_LEN);
		span->has_parent = false;
		return OTLP_OK;
	}
	/* W3C Trace Context §3.1.2: parent-id MUST NOT be all-zero.
	 * Use NULL to clear. */
	if (otlp_id_is_all_zero(parent, OTLP_SPAN_ID_LEN))
		return OTLP_ERR_INVALID_ARGUMENT;
	memcpy(span->parent_span_id, parent, OTLP_SPAN_ID_LEN);
	span->has_parent = true;
	return OTLP_OK;
}

/* ── Timing ───────────────────────────────────────────────────── */

otlp_status_t
otlp_span_set_start_time(otlp_span_t *span, uint64_t unix_nano)
{
	if (!span)
		return OTLP_ERR_NULL;
	span->start_time_unix_nano = unix_nano;
	span->has_start_time = true;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_end_time(otlp_span_t *span, uint64_t unix_nano)
{
	if (!span)
		return OTLP_ERR_NULL;
	span->end_time_unix_nano = unix_nano;
	span->has_end_time = true;
	return OTLP_OK;
}

otlp_status_t
otlp_span_mark_start(otlp_span_t *span)
{
	if (!span)
		return OTLP_ERR_NULL;
	return otlp_platform_now_unix_nano(&span->start_time_unix_nano) ==
			OTLP_OK
		? (span->has_start_time = true, OTLP_OK)
		: OTLP_ERR_NETWORK;
}

otlp_status_t
otlp_span_mark_end(otlp_span_t *span)
{
	if (!span)
		return OTLP_ERR_NULL;
	return otlp_platform_now_unix_nano(&span->end_time_unix_nano) == OTLP_OK
		? (span->has_end_time = true, OTLP_OK)
		: OTLP_ERR_NETWORK;
}

/* ── Metadata ─────────────────────────────────────────────────── */

otlp_status_t
otlp_span_set_kind(otlp_span_t *span, otlp_span_kind_t kind)
{
	if (!span)
		return OTLP_ERR_NULL;
	span->kind = kind;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_name(otlp_span_t *span, const char *name)
{
	char *new_name;

	if (!span)
		return OTLP_ERR_NULL;
	new_name = otlp_dup_str(name ? name : "");
	if (!new_name)
		return OTLP_ERR_NOMEM;
	otlp_free(span->name);
	span->name = new_name;
	return OTLP_OK;
}

/* ── Attributes ───────────────────────────────────────────────── */

otlp_status_t
otlp_span_set_attribute_string(otlp_span_t *span,
	const char *key,
	const char *value)
{
	struct otlp_attribute *a;
	char *val_copy;
	otlp_status_t st;

	val_copy = otlp_dup_str(value ? value : "");
	if (!val_copy)
		return OTLP_ERR_NOMEM;
	st = attr_reserve(span, key, &a);
	if (st != OTLP_OK)
	{
		otlp_free(val_copy);
		return st;
	}
	a->type = OTLP_ATTR_STRING;
	a->v.string_val = val_copy;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_attribute_int(otlp_span_t *span, const char *key, int64_t value)
{
	struct otlp_attribute *a;
	otlp_status_t st;

	st = attr_reserve(span, key, &a);
	if (st != OTLP_OK)
		return st;
	a->type = OTLP_ATTR_INT64;
	a->v.int64_val = value;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_attribute_double(otlp_span_t *span, const char *key, double value)
{
	struct otlp_attribute *a;
	otlp_status_t st;

	st = attr_reserve(span, key, &a);
	if (st != OTLP_OK)
		return st;
	a->type = OTLP_ATTR_DOUBLE;
	a->v.double_val = value;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_attribute_bool(otlp_span_t *span, const char *key, bool value)
{
	struct otlp_attribute *a;
	otlp_status_t st;

	st = attr_reserve(span, key, &a);
	if (st != OTLP_OK)
		return st;
	a->type = OTLP_ATTR_BOOL;
	a->v.bool_val = value;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_attribute_bytes(otlp_span_t *span,
	const char *key,
	const uint8_t *bytes,
	size_t len)
{
	struct otlp_attribute *a;
	uint8_t *bytes_copy;
	otlp_status_t st;

	if (len > 0 && !bytes)
		return OTLP_ERR_NULL;
	bytes_copy = otlp_dup_bytes(bytes, len);
	if (len > 0 && !bytes_copy)
		return OTLP_ERR_NOMEM;
	st = attr_reserve(span, key, &a);
	if (st != OTLP_OK)
	{
		otlp_free(bytes_copy);
		return st;
	}
	a->type = OTLP_ATTR_BYTES;
	a->v.bytes_val.data = bytes_copy;
	a->v.bytes_val.len = len;
	return OTLP_OK;
}

/* ── Status ───────────────────────────────────────────────────── */

otlp_status_t
otlp_span_set_status(otlp_span_t *span,
	otlp_status_code_t code,
	const char *description)
{
	char *msg_copy = NULL;

	if (!span)
		return OTLP_ERR_NULL;
	if (description)
	{
		msg_copy = otlp_dup_str(description);
		if (!msg_copy)
			return OTLP_ERR_NOMEM;
	}
	otlp_free(span->status_message);
	span->status_message = msg_copy;
	span->status_code = code;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_sampled(otlp_span_t *span, bool sampled)
{
	if (!span)
		return OTLP_ERR_NULL;
	span->sampled = sampled;
	return OTLP_OK;
}

/* ── Deferred OTLP fields ─────────────────────────────────────── */

otlp_status_t
otlp_span_add_event(otlp_span_t *span,
	const char *name,
	uint64_t time_unix_nano)
{
	char *name_copy;

	if (!span)
		return OTLP_ERR_NULL;
	if (!name)
		return OTLP_ERR_NULL;
	if (span->n_events >= OTLP_SPAN_MAX_EVENTS)
		return OTLP_ERR_OVERFLOW;
	name_copy = otlp_dup_str(name);
	if (!name_copy)
		return OTLP_ERR_NOMEM;
	span->events[span->n_events].name = name_copy;
	span->events[span->n_events].time_unix_nano = time_unix_nano;
	span->n_events++;
	return OTLP_OK;
}

otlp_status_t
otlp_span_add_link(otlp_span_t *span,
	const uint8_t *trace_id,
	const uint8_t *span_id)
{
	if (!span)
		return OTLP_ERR_NULL;
	if (!trace_id || !span_id)
		return OTLP_ERR_NULL;
	if (span->n_links >= OTLP_SPAN_MAX_LINKS)
		return OTLP_ERR_OVERFLOW;
	memcpy(span->links[span->n_links].trace_id,
		trace_id,
		OTLP_TRACE_ID_LEN);
	memcpy(span->links[span->n_links].span_id, span_id, OTLP_SPAN_ID_LEN);
	span->n_links++;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_trace_state(otlp_span_t *span, const char *trace_state)
{
	char *copy = NULL;

	if (!span)
		return OTLP_ERR_NULL;
	if (trace_state)
	{
		copy = otlp_dup_str(trace_state);
		if (!copy)
			return OTLP_ERR_NOMEM;
	}
	otlp_free(span->trace_state);
	span->trace_state = copy;
	return OTLP_OK;
}

/* Common prelude for the event/link attribute setters: validate,
 * target the most-recently-added event/link, reserve a slot in
 * its lazy attribute array. The caller fills the typed value and
 * increments the owner's count on success. */
static otlp_status_t
event_attr_slot(otlp_span_t *span,
	const char *key,
	struct otlp_event **ev_out,
	struct otlp_attribute **a_out)
{
	if (!span || !key)
		return OTLP_ERR_NULL;
	if (span->n_events == 0)
		return OTLP_ERR_INVALID_ARGUMENT;
	*ev_out = &span->events[span->n_events - 1];
	return otlp_attr_list_reserve(&(*ev_out)->attrs,
		&(*ev_out)->n_attrs,
		OTLP_EVENT_MAX_ATTRS,
		key,
		a_out);
}

static otlp_status_t
link_attr_slot(otlp_span_t *span,
	const char *key,
	struct otlp_link **lk_out,
	struct otlp_attribute **a_out)
{
	if (!span || !key)
		return OTLP_ERR_NULL;
	if (span->n_links == 0)
		return OTLP_ERR_INVALID_ARGUMENT;
	*lk_out = &span->links[span->n_links - 1];
	return otlp_attr_list_reserve(&(*lk_out)->attrs,
		&(*lk_out)->n_attrs,
		OTLP_LINK_MAX_ATTRS,
		key,
		a_out);
}

otlp_status_t
otlp_span_set_event_attribute_string(otlp_span_t *span,
	const char *key,
	const char *value)
{
	struct otlp_event *ev;
	struct otlp_attribute *a;
	char *vc;
	otlp_status_t st;

	vc = otlp_dup_str(value ? value : "");
	if (!vc)
		return OTLP_ERR_NOMEM;
	st = event_attr_slot(span, key, &ev, &a);
	if (st != OTLP_OK)
	{
		otlp_free(vc);
		return st;
	}
	a->type = OTLP_ATTR_STRING;
	a->v.string_val = vc;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_event_attribute_int(otlp_span_t *span,
	const char *key,
	int64_t value)
{
	struct otlp_event *ev;
	struct otlp_attribute *a;
	otlp_status_t st = event_attr_slot(span, key, &ev, &a);

	if (st != OTLP_OK)
		return st;
	a->type = OTLP_ATTR_INT64;
	a->v.int64_val = value;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_event_attribute_double(otlp_span_t *span,
	const char *key,
	double value)
{
	struct otlp_event *ev;
	struct otlp_attribute *a;
	otlp_status_t st = event_attr_slot(span, key, &ev, &a);

	if (st != OTLP_OK)
		return st;
	a->type = OTLP_ATTR_DOUBLE;
	a->v.double_val = value;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_event_attribute_bool(otlp_span_t *span,
	const char *key,
	bool value)
{
	struct otlp_event *ev;
	struct otlp_attribute *a;
	otlp_status_t st = event_attr_slot(span, key, &ev, &a);

	if (st != OTLP_OK)
		return st;
	a->type = OTLP_ATTR_BOOL;
	a->v.bool_val = value;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_event_attribute_bytes(otlp_span_t *span,
	const char *key,
	const uint8_t *bytes,
	size_t len)
{
	struct otlp_event *ev;
	struct otlp_attribute *a;
	uint8_t *bytes_copy;
	otlp_status_t st;

	if (len > 0 && !bytes)
		return OTLP_ERR_NULL;
	bytes_copy = otlp_dup_bytes(bytes, len);
	if (len > 0 && !bytes_copy)
		return OTLP_ERR_NOMEM;
	st = event_attr_slot(span, key, &ev, &a);
	if (st != OTLP_OK)
	{
		otlp_free(bytes_copy);
		return st;
	}
	a->type = OTLP_ATTR_BYTES;
	a->v.bytes_val.data = bytes_copy;
	a->v.bytes_val.len = len;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_link_attribute_string(otlp_span_t *span,
	const char *key,
	const char *value)
{
	struct otlp_link *lk;
	struct otlp_attribute *a;
	char *vc;
	otlp_status_t st;

	vc = otlp_dup_str(value ? value : "");
	if (!vc)
		return OTLP_ERR_NOMEM;
	st = link_attr_slot(span, key, &lk, &a);
	if (st != OTLP_OK)
	{
		otlp_free(vc);
		return st;
	}
	a->type = OTLP_ATTR_STRING;
	a->v.string_val = vc;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_link_attribute_int(otlp_span_t *span,
	const char *key,
	int64_t value)
{
	struct otlp_link *lk;
	struct otlp_attribute *a;
	otlp_status_t st = link_attr_slot(span, key, &lk, &a);

	if (st != OTLP_OK)
		return st;
	a->type = OTLP_ATTR_INT64;
	a->v.int64_val = value;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_link_attribute_double(otlp_span_t *span,
	const char *key,
	double value)
{
	struct otlp_link *lk;
	struct otlp_attribute *a;
	otlp_status_t st = link_attr_slot(span, key, &lk, &a);

	if (st != OTLP_OK)
		return st;
	a->type = OTLP_ATTR_DOUBLE;
	a->v.double_val = value;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_link_attribute_bool(otlp_span_t *span,
	const char *key,
	bool value)
{
	struct otlp_link *lk;
	struct otlp_attribute *a;
	otlp_status_t st = link_attr_slot(span, key, &lk, &a);

	if (st != OTLP_OK)
		return st;
	a->type = OTLP_ATTR_BOOL;
	a->v.bool_val = value;
	return OTLP_OK;
}

otlp_status_t
otlp_span_set_link_attribute_bytes(otlp_span_t *span,
	const char *key,
	const uint8_t *bytes,
	size_t len)
{
	struct otlp_link *lk;
	struct otlp_attribute *a;
	uint8_t *bytes_copy;
	otlp_status_t st;

	if (len > 0 && !bytes)
		return OTLP_ERR_NULL;
	bytes_copy = otlp_dup_bytes(bytes, len);
	if (len > 0 && !bytes_copy)
		return OTLP_ERR_NOMEM;
	st = link_attr_slot(span, key, &lk, &a);
	if (st != OTLP_OK)
	{
		otlp_free(bytes_copy);
		return st;
	}
	a->type = OTLP_ATTR_BYTES;
	a->v.bytes_val.data = bytes_copy;
	a->v.bytes_val.len = len;
	return OTLP_OK;
}

/* ── Internal accessors (see span_internal.h) ─────────────────── */

const char *
otlp_span_get_name(const otlp_span_t *span)
{
	return span ? span->name : NULL;
}

const uint8_t *
otlp_span_get_trace_id(const otlp_span_t *span)
{
	return span ? span->trace_id : NULL;
}

const uint8_t *
otlp_span_get_span_id(const otlp_span_t *span)
{
	return span ? span->span_id : NULL;
}

bool
otlp_span_has_parent(const otlp_span_t *span)
{
	return span ? span->has_parent : false;
}

const uint8_t *
otlp_span_get_parent_span_id(const otlp_span_t *span)
{
	return span ? span->parent_span_id : NULL;
}

otlp_span_kind_t
otlp_span_get_kind(const otlp_span_t *span)
{
	return span ? span->kind : OTLP_SPAN_KIND_UNSPECIFIED;
}

bool
otlp_span_has_start_time(const otlp_span_t *span)
{
	return span ? span->has_start_time : false;
}

uint64_t
otlp_span_get_start_time(const otlp_span_t *span)
{
	return span ? span->start_time_unix_nano : 0;
}

bool
otlp_span_has_end_time(const otlp_span_t *span)
{
	return span ? span->has_end_time : false;
}

uint64_t
otlp_span_get_end_time(const otlp_span_t *span)
{
	return span ? span->end_time_unix_nano : 0;
}

otlp_status_code_t
otlp_span_get_status_code(const otlp_span_t *span)
{
	return span ? span->status_code : OTLP_STATUS_CODE_UNSET;
}

const char *
otlp_span_get_status_message(const otlp_span_t *span)
{
	return span ? span->status_message : NULL;
}

bool
otlp_span_is_sampled(const otlp_span_t *span)
{
	return span ? span->sampled : false;
}

const struct otlp_event *
otlp_span_get_events(const otlp_span_t *span, size_t *n_out)
{
	if (!span)
	{
		if (n_out)
			*n_out = 0;
		return NULL;
	}
	if (n_out)
		*n_out = span->n_events;
	return span->events;
}

const struct otlp_link *
otlp_span_get_links(const otlp_span_t *span, size_t *n_out)
{
	if (!span)
	{
		if (n_out)
			*n_out = 0;
		return NULL;
	}
	if (n_out)
		*n_out = span->n_links;
	return span->links;
}

const char *
otlp_span_get_trace_state(const otlp_span_t *span)
{
	return span ? span->trace_state : NULL;
}

size_t
otlp_span_struct_size(void)
{
	return sizeof(struct otlp_span);
}

const struct otlp_attribute *
otlp_span_get_attrs(const otlp_span_t *span, size_t *n_out)
{
	if (!span)
	{
		if (n_out)
			*n_out = 0;
		return NULL;
	}
	if (n_out)
		*n_out = span->n_attrs;
	return span->attrs;
}

otlp_span_t *
otlp_span_clone(const otlp_span_t *src)
{
	otlp_span_t *dst;
	const struct otlp_attribute *attrs;
	const struct otlp_event *events;
	const struct otlp_link *links;
	size_t n_attrs, n_events, n_links;
	size_t i;

	if (!src)
		return NULL;
	dst = otlp_span_create(otlp_span_get_name(src));
	if (!dst)
		return NULL;

	memcpy(dst->trace_id, src->trace_id, OTLP_TRACE_ID_LEN);
	memcpy(dst->span_id, src->span_id, OTLP_SPAN_ID_LEN);
	dst->has_parent = src->has_parent;
	if (src->has_parent)
		memcpy(dst->parent_span_id,
			src->parent_span_id,
			OTLP_SPAN_ID_LEN);
	dst->has_start_time = src->has_start_time;
	dst->start_time_unix_nano = src->start_time_unix_nano;
	dst->has_end_time = src->has_end_time;
	dst->end_time_unix_nano = src->end_time_unix_nano;
	dst->kind = src->kind;
	dst->status_code = src->status_code;
	dst->sampled = src->sampled;
	if (src->status_message)
	{
		dst->status_message = otlp_dup_str(src->status_message);
		if (!dst->status_message)
		{
			otlp_span_free(dst);
			return NULL;
		}
	}
	if (src->trace_state)
	{
		dst->trace_state = otlp_dup_str(src->trace_state);
		if (!dst->trace_state)
		{
			otlp_span_free(dst);
			return NULL;
		}
	}

	attrs = otlp_span_get_attrs(src, &n_attrs);
	if (n_attrs > 0)
	{
		if (otlp_attribute_copy_all(dst->attrs, attrs, n_attrs) !=
			OTLP_OK)
		{
			otlp_span_free(dst);
			return NULL;
		}
		dst->n_attrs = n_attrs;
	}

	events = otlp_span_get_events(src, &n_events);
	for (i = 0; i < n_events; i++)
	{
		otlp_status_t st = otlp_span_add_event(
			dst, events[i].name, events[i].time_unix_nano);
		if (st != OTLP_OK)
		{
			otlp_span_free(dst);
			return NULL;
		}
		/* Copy event attributes (was missing — data loss bug).
		 * attrs is lazily allocated (v0.5.68); attr_list_copy
		 * calloc's a zeroed destination and is safe to free on
		 * failure. */
		if (events[i].n_attrs > 0)
		{
			struct otlp_event *ev = &dst->events[dst->n_events - 1];
			if (otlp_attr_list_copy(&ev->attrs,
				    &ev->n_attrs,
				    OTLP_EVENT_MAX_ATTRS,
				    events[i].attrs,
				    events[i].n_attrs) != OTLP_OK)
			{
				otlp_span_free(dst);
				return NULL;
			}
		}
	}

	links = otlp_span_get_links(src, &n_links);
	for (i = 0; i < n_links; i++)
	{
		otlp_status_t st = otlp_span_add_link(
			dst, links[i].trace_id, links[i].span_id);
		if (st != OTLP_OK)
		{
			otlp_span_free(dst);
			return NULL;
		}
		/* Copy link attributes (was missing — data loss bug).
		 * attrs is lazily allocated (v0.5.68); see event path. */
		if (links[i].n_attrs > 0)
		{
			struct otlp_link *lnk = &dst->links[dst->n_links - 1];
			if (otlp_attr_list_copy(&lnk->attrs,
				    &lnk->n_attrs,
				    OTLP_LINK_MAX_ATTRS,
				    links[i].attrs,
				    links[i].n_attrs) != OTLP_OK)
			{
				otlp_span_free(dst);
				return NULL;
			}
		}
	}
	return dst;
}
