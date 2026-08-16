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
	.alloc = malloc,
	.realloc = realloc,
	.free = free,
};

void
otlp_set_allocator(const otlp_allocator_t *alloc)
{
	if (alloc)
		g_allocator = *alloc;
	else
	{
		g_allocator.alloc = malloc;
		g_allocator.realloc = realloc;
		g_allocator.free = free;
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
	void *p = g_allocator.alloc(total);

	if (p)
		memset(p, 0, total);
	return p;
}

/* ── String / byte duplication ────────────────────────────────── */

char *
otlp_dup_str(const char *s)
{
	size_t len;
	char *out;

	if (!s)
		return NULL;
	len = strlen(s);
	if (len == SIZE_MAX)
		return NULL; /* len + 1 would overflow */
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
otlp_attribute_release_value(struct otlp_attribute *a)
{
	size_t i;

	if (!a)
		return;
	switch (a->type)
	{
		case OTLP_ATTR_ARRAY:
			if (a->v.array_val)
			{
				for (i = 0; i < a->v.array_val->n; i++)
					otlp_attribute_free(
						&a->v.array_val->items[i]);
				otlp_free(a->v.array_val->items);
				otlp_free(a->v.array_val);
				a->v.array_val = NULL;
			}
			break;
		case OTLP_ATTR_KVLIST:
			if (a->v.kvlist_val)
			{
				for (i = 0; i < a->v.kvlist_val->n; i++)
				{
					otlp_free(a->v.kvlist_val->entries[i]
							.key);
					otlp_attribute_free(
						&a->v.kvlist_val->entries[i]
							.value);
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
	/* Safe empty state: the slot stays valid for a typed refill
	 * and for the free path. */
	a->type = OTLP_ATTR_STRING;
	a->v.string_val = NULL;
}

void
otlp_attribute_free(struct otlp_attribute *a)
{
	if (!a)
		return;
	otlp_attribute_release_value(a);
	otlp_free(a->key);
	a->key = NULL;
}

/* ── Attribute copy ───────────────────────────────────────────── */

/* Deep-copy one attribute, including a nested array/kvlist tree.
 * Safe on partial failure: on return != OTLP_OK the destination
 * is left in a state otlp_attribute_free can handle (NULL-safe). */
static otlp_status_t
attr_copy_one(struct otlp_attribute *dst, const struct otlp_attribute *src)
{
	size_t i;

	dst->key = NULL;
	dst->type = OTLP_ATTR_STRING;
	dst->v.string_val = NULL;
	if (src->key)
	{
		dst->key = otlp_dup_str(src->key);
		if (!dst->key)
			return OTLP_ERR_NOMEM;
	}
	switch (src->type)
	{
		case OTLP_ATTR_STRING:
			dst->type = OTLP_ATTR_STRING;
			if (src->v.string_val)
			{
				dst->v.string_val =
					otlp_dup_str(src->v.string_val);
				if (!dst->v.string_val)
					return OTLP_ERR_NOMEM;
			}
			break;
		case OTLP_ATTR_INT64:
			dst->type = OTLP_ATTR_INT64;
			dst->v.int64_val = src->v.int64_val;
			break;
		case OTLP_ATTR_DOUBLE:
			dst->type = OTLP_ATTR_DOUBLE;
			dst->v.double_val = src->v.double_val;
			break;
		case OTLP_ATTR_BOOL:
			dst->type = OTLP_ATTR_BOOL;
			dst->v.bool_val = src->v.bool_val;
			break;
		case OTLP_ATTR_BYTES:
			dst->type = OTLP_ATTR_BYTES;
			dst->v.bytes_val.len = src->v.bytes_val.len;
			if (src->v.bytes_val.len > 0)
			{
				dst->v.bytes_val.data =
					otlp_malloc(src->v.bytes_val.len);
				if (!dst->v.bytes_val.data)
					return OTLP_ERR_NOMEM;
				memcpy(dst->v.bytes_val.data,
					src->v.bytes_val.data,
					src->v.bytes_val.len);
			}
			break;
		case OTLP_ATTR_ARRAY:
		{
			const struct otlp_attr_array *arr = src->v.array_val;

			dst->type = OTLP_ATTR_ARRAY;
			if (!arr)
				break;
			dst->v.array_val = otlp_calloc(1, sizeof(*arr));
			if (!dst->v.array_val)
				return OTLP_ERR_NOMEM;
			if (arr->n > SIZE_MAX / sizeof(*arr->items))
			{
				otlp_free(dst->v.array_val);
				dst->v.array_val = NULL;
				return OTLP_ERR_NOMEM;
			}
			dst->v.array_val->items =
				otlp_calloc(arr->n, sizeof(*arr->items));
			if (!dst->v.array_val->items)
			{
				otlp_free(dst->v.array_val);
				dst->v.array_val = NULL;
				return OTLP_ERR_NOMEM;
			}
			dst->v.array_val->n = arr->n;
			for (i = 0; i < arr->n; i++)
				if (attr_copy_one(&dst->v.array_val->items[i],
					    &arr->items[i]) != OTLP_OK)
				{
					/* The failing item may be partially
					 * built; free is safe on partial
					 * state, then free the fully-built
					 * predecessors. */
					do
						otlp_attribute_free(
							&dst->v.array_val
								->items[i]);
					while (i-- > 0);
					otlp_free(dst->v.array_val->items);
					otlp_free(dst->v.array_val);
					dst->v.array_val = NULL;
					return OTLP_ERR_NOMEM;
				}
			break;
		}
		case OTLP_ATTR_KVLIST:
		{
			const struct otlp_attr_kvlist *kvl = src->v.kvlist_val;

			dst->type = OTLP_ATTR_KVLIST;
			if (!kvl)
				break;
			dst->v.kvlist_val = otlp_calloc(1, sizeof(*kvl));
			if (!dst->v.kvlist_val)
				return OTLP_ERR_NOMEM;
			if (kvl->n > SIZE_MAX / sizeof(*kvl->entries))
			{
				otlp_free(dst->v.kvlist_val);
				dst->v.kvlist_val = NULL;
				return OTLP_ERR_NOMEM;
			}
			dst->v.kvlist_val->entries =
				otlp_calloc(kvl->n, sizeof(*kvl->entries));
			if (!dst->v.kvlist_val->entries)
			{
				otlp_free(dst->v.kvlist_val);
				dst->v.kvlist_val = NULL;
				return OTLP_ERR_NOMEM;
			}
			dst->v.kvlist_val->n = kvl->n;
			for (i = 0; i < kvl->n; i++)
			{
				struct otlp_attr_kvlist_entry *e =
					&dst->v.kvlist_val->entries[i];

				if (kvl->entries[i].key)
				{
					e->key = otlp_dup_str(
						kvl->entries[i].key);
					if (!e->key)
						goto kvl_fail;
				}
				if (attr_copy_one(&e->value,
					    &kvl->entries[i].value) != OTLP_OK)
					goto kvl_fail;
				continue;
			kvl_fail:
				do
				{
					otlp_free(dst->v.kvlist_val->entries[i]
							.key);
					otlp_attribute_free(
						&dst->v.kvlist_val->entries[i]
							.value);
				} while (i-- > 0);
				otlp_free(dst->v.kvlist_val->entries);
				otlp_free(dst->v.kvlist_val);
				dst->v.kvlist_val = NULL;
				return OTLP_ERR_NOMEM;
			}
			break;
		}
		default:
			return OTLP_ERR_NOMEM;
	}
	return OTLP_OK;
}

otlp_status_t
otlp_attribute_copy_all(struct otlp_attribute *dst,
	const struct otlp_attribute *src,
	size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (attr_copy_one(&dst[i], &src[i]) != OTLP_OK)
			goto fail;
	return OTLP_OK;

fail:
	/* Free partial copies. The item at index i may be partially
	 * built; otlp_attribute_free is safe on partial state — it
	 * no-ops on NULL fields and recurses into built trees. */
	otlp_attribute_free(&dst[i]);
	while (i > 0)
	{
		i--;
		otlp_attribute_free(&dst[i]);
	}
	return OTLP_ERR_NOMEM;
}

/* ── ArrayValue / KeyValueList builders ───────────────────────── */

/* Fill one internal attribute from a public scalar value. */
static otlp_status_t
value_fill(struct otlp_attribute *a, const otlp_value_t *v)
{
	a->key = NULL;
	a->type = OTLP_ATTR_STRING;
	a->v.string_val = NULL;
	switch (v->type)
	{
		case OTLP_VALUE_STRING:
			a->type = OTLP_ATTR_STRING;
			a->v.string_val = otlp_dup_str(
				v->v.string_val ? v->v.string_val : "");
			return a->v.string_val ? OTLP_OK : OTLP_ERR_NOMEM;
		case OTLP_VALUE_BOOL:
			a->type = OTLP_ATTR_BOOL;
			a->v.bool_val = v->v.bool_val;
			return OTLP_OK;
		case OTLP_VALUE_INT64:
			a->type = OTLP_ATTR_INT64;
			a->v.int64_val = v->v.int64_val;
			return OTLP_OK;
		case OTLP_VALUE_DOUBLE:
			a->type = OTLP_ATTR_DOUBLE;
			a->v.double_val = v->v.double_val;
			return OTLP_OK;
		case OTLP_VALUE_BYTES:
			a->type = OTLP_ATTR_BYTES;
			a->v.bytes_val.len = v->v.bytes_val.len;
			if (v->v.bytes_val.len > 0)
			{
				a->v.bytes_val.data =
					otlp_dup_bytes(v->v.bytes_val.data,
						v->v.bytes_val.len);
				if (!a->v.bytes_val.data)
					return OTLP_ERR_NOMEM;
			}
			return OTLP_OK;
		default:
			return OTLP_ERR_INVALID_ARGUMENT;
	}
}

otlp_status_t
otlp_attr_array_build(const otlp_value_t *items,
	size_t n,
	struct otlp_attr_array **out)
{
	struct otlp_attr_array *arr;
	size_t i;

	*out = NULL;
	if (n > 0 && !items)
		return OTLP_ERR_NULL;
	if (n > SIZE_MAX / sizeof(*arr->items))
		return OTLP_ERR_INVALID_ARGUMENT;
	arr = otlp_calloc(1, sizeof(*arr));
	if (!arr)
		return OTLP_ERR_NOMEM;
	if (n > 0)
	{
		arr->items = otlp_calloc(n, sizeof(*arr->items));
		if (!arr->items)
		{
			otlp_free(arr);
			return OTLP_ERR_NOMEM;
		}
		arr->n = n;
	}
	for (i = 0; i < n; i++)
		if (value_fill(&arr->items[i], &items[i]) != OTLP_OK)
		{
			do
				otlp_attribute_free(&arr->items[i]);
			while (i-- > 0);
			otlp_free(arr->items);
			otlp_free(arr);
			return OTLP_ERR_NOMEM;
		}
	*out = arr;
	return OTLP_OK;
}

otlp_status_t
otlp_attr_kvlist_build(const otlp_kv_t *entries,
	size_t n,
	struct otlp_attr_kvlist **out)
{
	struct otlp_attr_kvlist *kvl;
	size_t i;

	*out = NULL;
	if (n > 0 && !entries)
		return OTLP_ERR_NULL;
	if (n > SIZE_MAX / sizeof(*kvl->entries))
		return OTLP_ERR_INVALID_ARGUMENT;
	kvl = otlp_calloc(1, sizeof(*kvl));
	if (!kvl)
		return OTLP_ERR_NOMEM;
	if (n > 0)
	{
		kvl->entries = otlp_calloc(n, sizeof(*kvl->entries));
		if (!kvl->entries)
		{
			otlp_free(kvl);
			return OTLP_ERR_NOMEM;
		}
		kvl->n = n;
	}
	for (i = 0; i < n; i++)
	{
		if (!entries[i].key ||
			value_fill(&kvl->entries[i].value, &entries[i].value) !=
				OTLP_OK)
		{
			do
			{
				otlp_free(kvl->entries[i].key);
				otlp_attribute_free(&kvl->entries[i].value);
			} while (i-- > 0);
			otlp_free(kvl->entries);
			otlp_free(kvl);
			return entries[i].key ? OTLP_ERR_NOMEM : OTLP_ERR_NULL;
		}
		kvl->entries[i].key = otlp_dup_str(entries[i].key);
		if (!kvl->entries[i].key)
		{
			do
			{
				otlp_free(kvl->entries[i].key);
				otlp_attribute_free(&kvl->entries[i].value);
			} while (i-- > 0);
			otlp_free(kvl->entries);
			otlp_free(kvl);
			return OTLP_ERR_NOMEM;
		}
	}
	*out = kvl;
	return OTLP_OK;
}

void
otlp_attr_array_free(struct otlp_attr_array *arr)
{
	size_t i;

	if (!arr)
		return;
	for (i = 0; i < arr->n; i++)
		otlp_attribute_free(&arr->items[i]);
	otlp_free(arr->items);
	otlp_free(arr);
}

void
otlp_attr_kvlist_free(struct otlp_attr_kvlist *kvl)
{
	size_t i;

	if (!kvl)
		return;
	for (i = 0; i < kvl->n; i++)
	{
		otlp_free(kvl->entries[i].key);
		otlp_attribute_free(&kvl->entries[i].value);
	}
	otlp_free(kvl->entries);
	otlp_free(kvl);
}

/* ── Lazy attribute lists ─────────────────────────────────────── */

bool
otlp_attr_list_find(const struct otlp_attribute *attrs,
	size_t n,
	const char *key,
	size_t *idx_out)
{
	size_t i;

	if (!attrs || !key)
		return false;
	for (i = 0; i < n; i++)
		if (attrs[i].key && strcmp(attrs[i].key, key) == 0)
		{
			if (idx_out)
				*idx_out = i;
			return true;
		}
	return false;
}

otlp_status_t
otlp_attr_list_reserve(struct otlp_attribute **attrs,
	size_t *n,
	size_t cap,
	const char *key,
	struct otlp_attribute **out)
{
	struct otlp_attribute *slot;
	char *kc;
	size_t idx;

	if (!attrs || !n || !out || !key)
		return OTLP_ERR_NULL;
	/* Upsert: an existing key's slot is reused (old value
	 * released, count unchanged, position preserved). Overwrite
	 * succeeds even at cap. */
	if (otlp_attr_list_find(*attrs, *n, key, &idx))
	{
		slot = &(*attrs)[idx];
		otlp_attribute_release_value(slot);
		*out = slot;
		return OTLP_OK;
	}
	if (*n >= cap)
		return OTLP_ERR_OVERFLOW;
	/* Lazy-allocate the array on first use; an attribute-less
	 * object costs one NULL pointer (see internal_util.h). */
	if (!*attrs)
	{
		*attrs = otlp_calloc(cap, sizeof(**attrs));
		if (!*attrs)
			return OTLP_ERR_NOMEM;
	}
	kc = otlp_dup_str(key);
	if (!kc)
		return OTLP_ERR_NOMEM;
	slot = &(*attrs)[*n];
	slot->key = kc;
	/* Zero the union so cleanup paths don't see garbage. */
	slot->v.string_val = NULL;
	slot->type = OTLP_ATTR_STRING;
	(*n)++;
	*out = slot;
	return OTLP_OK;
}

otlp_status_t
otlp_attr_list_copy(struct otlp_attribute **dst,
	size_t *n_dst,
	size_t cap,
	const struct otlp_attribute *src,
	size_t n_src)
{
	if (n_src == 0)
		return OTLP_OK;
	if (!dst || !n_dst || !src)
		return OTLP_ERR_NULL;
	if (n_src > cap)
		return OTLP_ERR_OVERFLOW;
	/* Zeroed destination so the free path is safe if the copy
	 * fails midway; copy_all frees partial slots itself. */
	*dst = otlp_calloc(cap, sizeof(**dst));
	if (!*dst)
		return OTLP_ERR_NOMEM;
	if (otlp_attribute_copy_all(*dst, src, n_src) != OTLP_OK)
	{
		otlp_free(*dst);
		*dst = NULL;
		return OTLP_ERR_NOMEM;
	}
	*n_dst = n_src;
	return OTLP_OK;
}

void
otlp_attr_list_free(struct otlp_attribute **attrs, size_t *n)
{
	size_t i;

	if (!attrs || !*attrs)
	{
		if (n)
			*n = 0;
		return;
	}
	for (i = 0; n && i < *n; i++)
		otlp_attribute_free(&(*attrs)[i]);
	otlp_free(*attrs);
	*attrs = NULL;
	if (n)
		*n = 0;
}

/* ── ID validation ────────────────────────────────────────────── */

bool
otlp_id_is_all_zero(const uint8_t *id, size_t len)
{
	size_t i;

	if (!id || len == 0)
		return true;
	for (i = 0; i < len; i++)
		if (id[i] != 0)
			return false;
	return true;
}
