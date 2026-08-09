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
#include "span_internal.h"

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

/* ── Recursive attribute free ─────────────────────────────────── */

void
otlp_attribute_free(struct otlp_attribute *a)
{
	size_t i;

	if (!a)
		return;
	switch (a->type) {
	case OTLP_ATTR_ARRAY:
		if (a->v.array_val) {
			for (i = 0; i < a->v.array_val->n; i++)
				otlp_attribute_free(&a->v.array_val->items[i]);
			otlp_free(a->v.array_val->items);
			otlp_free(a->v.array_val);
			a->v.array_val = NULL;
		}
		break;
	case OTLP_ATTR_KVLIST:
		if (a->v.kvlist_val) {
			for (i = 0; i < a->v.kvlist_val->n; i++) {
				otlp_free(a->v.kvlist_val->entries[i].key);
				otlp_attribute_free(&a->v.kvlist_val->entries[i].value);
			}
			otlp_free(a->v.kvlist_val->entries);
			otlp_free(a->v.kvlist_val);
			a->v.kvlist_val = NULL;
		}
		break;
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
		break;
	}
	otlp_free(a->key);
	a->key = NULL;
}

/* ── Attribute copy ───────────────────────────────────────────── */

otlp_status_t
otlp_attribute_copy_all(struct otlp_attribute *dst,
			const struct otlp_attribute *src,
			size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
	{
		dst[i].key = otlp_dup_str(src[i].key);
		if (!dst[i].key)
			goto fail;
		dst[i].type = src[i].type;
		switch (src[i].type)
		{
			case OTLP_ATTR_STRING:
				dst[i].v.string_val =
					otlp_dup_str(src[i].v.string_val);
				if (!dst[i].v.string_val)
					goto fail;
				break;
			case OTLP_ATTR_INT64:
				dst[i].v.int64_val = src[i].v.int64_val;
				break;
			case OTLP_ATTR_DOUBLE:
				dst[i].v.double_val = src[i].v.double_val;
				break;
			case OTLP_ATTR_BOOL:
				dst[i].v.bool_val = src[i].v.bool_val;
				break;
			case OTLP_ATTR_BYTES:
				dst[i].v.bytes_val.len = src[i].v.bytes_val.len;
				if (src[i].v.bytes_val.len > 0)
				{
					dst[i].v.bytes_val.data =
						otlp_malloc(src[i].v.bytes_val.len);
					if (!dst[i].v.bytes_val.data)
						goto fail;
					memcpy(dst[i].v.bytes_val.data,
					       src[i].v.bytes_val.data,
					       src[i].v.bytes_val.len);
				}
				break;
			default:
				break;
		}
	}
	return OTLP_OK;

fail:
	/* Free partial copies. */
	while (i > 0)
	{
		i--;
		otlp_attribute_free(&dst[i]);
	}
	return OTLP_ERR_NOMEM;
}
