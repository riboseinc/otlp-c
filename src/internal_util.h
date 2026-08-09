/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Internal utilities shared across src/ files. NOT part of the
 * public API; do not include from include/.
 */
#ifndef OTLP_C_INTERNAL_UTIL_H
#define OTLP_C_INTERNAL_UTIL_H

#include "span_internal.h"

#include <stddef.h>
#include <stdint.h>

/* ── Custom-allocator-backed wrappers ───────────────────────────
 *
 * All source .c files under src/ MUST use these instead of
 * malloc/free/realloc/calloc directly. The wrappers dispatch
 * through the global allocator set via otlp_set_allocator()
 * (see include/otlp-c/allocator.h).
 */
void  *otlp_malloc(size_t n);
void  *otlp_realloc(void *p, size_t n);
void   otlp_free(void *p);
void  *otlp_calloc(size_t count, size_t size);

/* ── String / byte duplication ────────────────────────────────── */

/* Heap-allocate a copy of `s` (NUL-terminated). NULL on OOM.
 * NULL input returns NULL. */
char	*otlp_dup_str(const char *s);

/* Heap-allocate a copy of `len` bytes from `src`. NULL on OOM or
 * when len > 0 && src == NULL. */
uint8_t *otlp_dup_bytes(const uint8_t *src, size_t len);

/* ── Attribute copy (shared by span/metric/log clones) ────────── */

/* Deep-copy `n` attributes from `src` to `dst`. dst must have at
 * least `n` slots. On failure, partial copies are freed. Returns
 * OTLP_OK or OTLP_ERR_NOMEM. DRY: used by otlp_metric_clone,
 * otlp_log_record_clone (and could replace the inline copy in
 * otlp_span_clone). */
otlp_status_t otlp_attribute_copy_all(struct otlp_attribute *dst,
				      const struct otlp_attribute *src,
				      size_t n);

#endif
