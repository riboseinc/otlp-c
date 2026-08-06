/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Internal utilities shared across src/ files. See internal_util.h.
 *
 * This file also implements the custom-allocator dispatch: all
 * otlp_malloc/otlp_free/otlp_realloc/otlp_calloc calls route through
 * the global otlp_allocator_t, which defaults to system malloc/free
 * but can be overridden via otlp_set_allocator() (public API).
 */
#include "internal_util.h"

#include <otlp-c/allocator.h>

#include <stdlib.h>
#include <string.h>

/* ── Global allocator state ───────────────────────────────────── */

static otlp_allocator_t g_allocator = {
	.alloc   = malloc,
	.realloc = realloc,
	.free    = free,
};

void
otlp_set_allocator(const otlp_allocator_t *alloc)
{
	if (alloc)
		g_allocator = *alloc;
	else {
		g_allocator.alloc   = malloc;
		g_allocator.realloc = realloc;
		g_allocator.free    = free;
	}
}

const otlp_allocator_t *
otlp_get_allocator(void)
{
	return &g_allocator;
}

/* ── Allocator-backed wrappers ────────────────────────────────── */

void *
otlp_malloc(size_t n)
{
	return g_allocator.alloc(n);
}

void *
otlp_realloc(void *p, size_t n)
{
	return g_allocator.realloc(p, n);
}

void
otlp_free(void *p)
{
	g_allocator.free(p);
}

void *
otlp_calloc(size_t count, size_t size)
{
	size_t total = count * size;
	void  *p     = g_allocator.alloc(total);

	if (p)
		memset(p, 0, total);
	return p;
}

/* ── String / byte duplication ────────────────────────────────── */

char *
otlp_dup_str(const char *s)
{
	size_t len;
	char  *out;

	if (!s)
		return NULL;
	len = strlen(s);
	out = otlp_malloc(len + 1);
	if (!out)
		return NULL;
	memcpy(out, s, len + 1);
	return out;
}

uint8_t *
otlp_dup_bytes(const uint8_t *src, size_t len)
{
	uint8_t *out;

	if (len == 0)
		return NULL;
	if (!src)
		return NULL;
	out = otlp_malloc(len);
	if (!out)
		return NULL;
	memcpy(out, src, len);
	return out;
}
