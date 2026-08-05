/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Internal utilities shared across src/ files. NOT part of the
 * public API; do not include from include/.
 *
 * The intent is to kill duplicated static helpers (`dup_str`,
 * `dup_bytes`) that drifted across span.c, tracer.c, exporter.c.
 */
#ifndef OTLP_C_INTERNAL_UTIL_H
#define OTLP_C_INTERNAL_UTIL_H

#include <stddef.h>
#include <stdint.h>

/* Heap-allocate a copy of `s` (NUL-terminated). NULL on OOM.
 * NULL input returns NULL. */
char	*otlp_dup_str(const char *s);

/* Heap-allocate a copy of `len` bytes from `src`. NULL on OOM or
 * when len > 0 && src == NULL. */
uint8_t *otlp_dup_bytes(const uint8_t *src, size_t len);

#endif
