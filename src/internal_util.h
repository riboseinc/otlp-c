/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Internal utilities shared across src/ files. NOT part of the
 * public API; do not include from include/.
 */
#ifndef OTLP_C_INTERNAL_UTIL_H
#define OTLP_C_INTERNAL_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* ── Custom-allocator-backed wrappers ───────────────────────────
 *
 * All src/*.c files MUST use these instead of malloc/free/realloc/
 * calloc directly. The wrappers dispatch through the global
 * allocator set via otlp_set_allocator() (see include/otlp-c/allocator.h).
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

#endif
