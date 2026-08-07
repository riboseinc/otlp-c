/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Slab allocator implementation. See include/otlp-c/slab.h.
 *
 * Design: a contiguous arena of `slot_size * capacity` bytes, with
 * a parallel bitmap tracking which slots are in use. Allocations
 * up to slot_size are served from a free slot (linear scan); larger
 * allocations and overflow fall through to malloc. Free detects
 * whether the pointer lies within the arena and routes accordingly.
 *
 * Single-threaded by design (no atomics, no locks). The typical
 * caller is a per-tracer or per-thread slab used during span
 * construction (which is single-threaded by API contract).
 *
 * Slot size and capacity are caller-controlled. For span-related
 * use, slot_size=128 covers most attribute keys + small string
 * values; capacity=256 covers a typical batch.
 */
#include <otlp-c/slab.h>

#include "internal_util.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define OTLP_SLAB_ALIGN _Alignof(void *)

struct otlp_slab {
	size_t	slot_size;     /* user-requested size, rounded up to alignment */
	size_t	capacity;
	uint8_t *arena;        /* slot_size * capacity bytes */
	bool	   *used; /* capacity bytes */
	size_t	in_use;
	otlp_slab_stats_t stats;
};

static size_t
round_up(size_t v, size_t align)
{
	return (v + align - 1) & ~(align - 1);
}

otlp_slab_t *
otlp_slab_create(size_t slot_size, size_t capacity)
{
	struct otlp_slab *s;
	size_t	       aligned_slot;

	if (slot_size == 0 || capacity == 0)
		return NULL;
	aligned_slot = round_up(slot_size, OTLP_SLAB_ALIGN);
	s = otlp_calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	s->slot_size = aligned_slot;
	s->capacity  = capacity;
	s->arena     = otlp_malloc(aligned_slot * capacity);
	if (!s->arena)
		goto fail;
	s->used      = otlp_calloc(capacity, sizeof(*s->used));
	if (!s->used)
		goto fail;
	s->stats.slot_size = aligned_slot;
	s->stats.capacity  = capacity;
	return s;
fail:
	otlp_free(s->arena);
	otlp_free(s->used);
	otlp_free(s);
	return NULL;
}

void
otlp_slab_free(otlp_slab_t *slab)
{
	if (!slab)
		return;
	otlp_free(slab->arena);
	otlp_free(slab->used);
	otlp_free(slab);
}

static int
ptr_in_arena(const struct otlp_slab *s, void *ptr)
{
	uintptr_t base = (uintptr_t) s->arena;
	uintptr_t end  = base + (s->slot_size * s->capacity);
	uintptr_t p    = (uintptr_t) ptr;

	return p >= base && p < end;
}

void *
otlp_slab_alloc(otlp_slab_t *slab, size_t size)
{
	if (!slab)
		return NULL;
	slab->stats.alloc_count++;
	if (size <= slab->slot_size) {
		for (size_t i = 0; i < slab->capacity; i++) {
			if (!slab->used[i]) {
				slab->used[i] = true;
				slab->in_use++;
				slab->stats.in_use	  = slab->in_use;
				slab->stats.slab_hits++;
				return slab->arena + (i * slab->slot_size);
			}
		}
	}
	/* Overflow or oversize: fall through to malloc. */
	slab->stats.malloc_fallbacks++;
	return otlp_malloc(size);
}

void
otlp_slab_free_ptr(otlp_slab_t *slab, void *ptr)
{
	if (!slab || !ptr)
		return;
	slab->stats.free_count++;
	if (ptr_in_arena(slab, ptr)) {
		size_t off = (size_t)((uintptr_t) ptr - (uintptr_t) slab->arena);
		size_t i   = off / slab->slot_size;

		if (i < slab->capacity && slab->used[i]) {
			slab->used[i] = false;
			slab->in_use--;
			slab->stats.in_use	    = slab->in_use;
			slab->stats.slab_free_hits++;
			return;
		}
		/* Pointer is in arena range but not a valid in-use slot —
		 * fall through to free() defensively (double-free is UB). */
	}
	slab->stats.malloc_free_fallbacks++;
	otlp_free(ptr);
}

void
otlp_slab_get_stats(const otlp_slab_t *slab, otlp_slab_stats_t *out)
{
	if (!out)
		return;
	if (!slab) {
		memset(out, 0, sizeof(*out));
		return;
	}
	*out = slab->stats;
}
