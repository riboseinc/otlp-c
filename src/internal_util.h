/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Internal utilities shared across src/ files. NOT part of the
 * public API; do not include from include/.
 */
#ifndef OTLP_C_INTERNAL_UTIL_H
#define OTLP_C_INTERNAL_UTIL_H

#include <otlp-c/status.h>
#include <otlp-c/value.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Forward declaration — the full struct is in span_internal.h.
 * We avoid including span_internal.h here to prevent a dependency
 * cycle (span.c → internal_util.h → span_internal.h). Only the
 * .c file needs the full definition. */
struct otlp_attribute;
struct otlp_attr_array;
struct otlp_attr_kvlist;

/* ── Custom-allocator-backed wrappers ───────────────────────────
 *
 * All source .c files under src/ MUST use these instead of
 * malloc/free/realloc/calloc directly. The wrappers dispatch
 * through the global allocator set via otlp_set_allocator()
 * (see include/otlp-c/allocator.h).
 */
void *
otlp_malloc(size_t n);
void *
otlp_realloc(void *p, size_t n);
void
otlp_free(void *p);
void *
otlp_calloc(size_t count, size_t size);

/* ── String / byte duplication ────────────────────────────────── */

/* Heap-allocate a copy of `s` (NUL-terminated). NULL on OOM.
 * NULL input returns NULL. */
char *
otlp_dup_str(const char *s);

/* Heap-allocate a copy of `len` bytes from `src`. NULL on OOM or
 * when len > 0 && src == NULL. */
uint8_t *
otlp_dup_bytes(const uint8_t *src, size_t len);

/* ── Attribute copy (shared by span/metric/log clones) ────────── */

/* Deep-copy `n` attributes from `src` to `dst`. dst must have at
 * least `n` slots. On failure, partial copies are freed. Returns
 * OTLP_OK or OTLP_ERR_NOMEM. DRY: used by otlp_metric_clone,
 * otlp_log_record_clone (and could replace the inline copy in
 * otlp_span_clone). */
otlp_status_t
otlp_attribute_copy_all(struct otlp_attribute *dst,
	const struct otlp_attribute *src,
	size_t n);

/* Release the attribute's union payload (string, bytes, or a
 * nested array/kvlist tree) and reset the type to a safe empty
 * value. The key is kept — this is the primitive for
 * replace-value-in-place (attribute upsert). */
void
otlp_attribute_release_value(struct otlp_attribute *a);

/* ── Lazy attribute lists ───────────────────────────────────────
 *
 * The shared storage model for attribute arrays on spans' events/
 * links, metrics, and log records: a cap-bounded array that is
 * NULL until the first attribute is set, so an attribute-less
 * object costs one pointer instead of a cap-sized inline array
 * (v0.5.68/v0.5.69). These functions own that model — the
 * attribute-bearing types just pass their (attrs, n_attrs, cap)
 * triple. DRY: one implementation instead of four copies.
 *
 * Upsert semantics (v0.5.73): attributes are a map — the OTLP
 * data model requires unique keys and the OTel API defines
 * setting an attribute as last-write-wins. Reserve finds an
 * existing key and reuses its slot (releasing the old value) or
 * appends a new one; duplicates can no longer reach the wire.
 */

/* Linear search for `key`. Returns true and writes the index to
 * *idx_out when found. */
bool
otlp_attr_list_find(const struct otlp_attribute *attrs,
	size_t n,
	const char *key,
	size_t *idx_out);

/* Reserve the slot for `key` and commit the count: if the key
 * already exists, its old value is released and that slot is
 * returned (count unchanged); otherwise a slot is appended at the
 * end and *n is incremented. Lazily calloc's the cap-sized array
 * on first use. Overwriting an existing key succeeds even at cap;
 * appending past cap returns OTLP_ERR_OVERFLOW.
 *
 * The caller fills type + value AFTER this returns. Because the
 * slot is already committed, the fill must not fail — duplicate
 * owned values (strings, bytes) BEFORE calling and free them if
 * reserve itself fails. Scalar types have no failure path. */
otlp_status_t
otlp_attr_list_reserve(struct otlp_attribute **attrs,
	size_t *n,
	size_t cap,
	const char *key,
	struct otlp_attribute **out);

/* Deep-copy n_src attributes into a freshly lazily-styled array.
 * On success *dst owns the copy and *n_dst == n_src. On failure
 * everything is freed and *dst is NULL. n_src == 0 is a no-op. */
otlp_status_t
otlp_attr_list_copy(struct otlp_attribute **dst,
	size_t *n_dst,
	size_t cap,
	const struct otlp_attribute *src,
	size_t n_src);

/* Free every attribute and the array itself; resets *attrs to
 * NULL and *n to 0. Safe on an already-NULL array. */
void
otlp_attr_list_free(struct otlp_attribute **attrs, size_t *n);

/* ── ArrayValue / KeyValueList trees ────────────────────────────
 *
 * Builders for the composite attribute types: they consume the
 * public flat inputs (arrays of scalar otlp_value_t) and return
 * fully-owned internal trees. Build FIRST, then reserve the
 * attribute slot, then attach — the reserve-and-fill contract
 * requires the fill to be non-failing assignments.
 */

/* Deep-build an ArrayValue tree from `n` scalar values. The
 * result is owned by the caller until attached to an attribute
 * slot; free with otlp_attr_array_free if never attached. */
otlp_status_t
otlp_attr_array_build(const otlp_value_t *items,
	size_t n,
	struct otlp_attr_array **out);

/* Deep-build a KeyValueList tree from `n` (key, value) entries.
 * Duplicate entry keys are kept as given — the uniqueness
 * contract applies to attribute keys, not list contents. */
otlp_status_t
otlp_attr_kvlist_build(const otlp_kv_t *entries,
	size_t n,
	struct otlp_attr_kvlist **out);

void
otlp_attr_array_free(struct otlp_attr_array *arr);
void
otlp_attr_kvlist_free(struct otlp_attr_kvlist *kvl);

/* ── ID validation ────────────────────────────────────────────── */

/* Returns true if all `len` bytes of `id` are zero. W3C Trace
 * Context §3.1.1/§3.1.2 forbids all-zero trace-id and parent-id;
 * callers must reject all-zero at set time so invalid IDs don't
 * reach the wire (where receivers reject them). */
bool
otlp_id_is_all_zero(const uint8_t *id, size_t len);

#endif
