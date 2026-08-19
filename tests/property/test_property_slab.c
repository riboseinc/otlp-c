/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property tests for the slab allocator.
 *
 *   prop_slab_roundtrip         — alloc then free leaves no slots used.
 *   prop_slab_slot_reuse        — alloc-free-alloc returns the same slot.
 *   prop_slab_oversize_fallback — oversize alloc goes to malloc.
 *   prop_slab_overflow_fallback — exhausting slots falls through to malloc.
 *   prop_slab_free_routes_arena_vs_malloc correctly.
 *   prop_slab_stats_consistent  — stats reflect actual operations.
 *   prop_slab_global_realloc_growth — realloc of an arena pointer
 *                                     moves out of the slab safely.
 */
#include "prng.h"
#include "property_harness.h"

#include "../src/internal_util.h"
#include <otlp-c/slab.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static int
prop_slab_roundtrip(uint64_t seed)
{
	struct prng p;
	otlp_slab_t *s = otlp_slab_create(64, 8);
	void *ptrs[8];
	otlp_slab_stats_t st;

	(void) seed;
	if (!s)
		return 0;
	prng_seed(&p, 0);
	for (size_t i = 0; i < 8; i++)
	{
		ptrs[i] = otlp_slab_alloc(s, 32);
		if (!ptrs[i])
		{
			otlp_slab_free(s);
			return 0;
		}
	}
	for (size_t i = 0; i < 8; i++)
		otlp_slab_free_ptr(s, ptrs[i]);
	otlp_slab_get_stats(s, &st);
	otlp_slab_free(s);
	return st.in_use == 0 && st.slab_hits == 8 && st.slab_free_hits == 8;
}

static int
prop_slab_slot_reuse(uint64_t seed)
{
	otlp_slab_t *s = otlp_slab_create(64, 8);
	void *p1, *p2;
	int ok = 0;

	(void) seed;
	if (!s)
		return 0;
	p1 = otlp_slab_alloc(s, 64);
	if (!p1)
		goto out;
	otlp_slab_free_ptr(s, p1);
	p2 = otlp_slab_alloc(s, 64);
	/* After freeing the only in-use slot, the next alloc must return
	 * the same slot (it's the only free one). */
	ok = (p1 == p2);
	otlp_slab_free_ptr(s, p2);
out:
	otlp_slab_free(s);
	return ok;
}

static int
prop_slab_oversize_fallback(uint64_t seed)
{
	otlp_slab_t *s = otlp_slab_create(32, 8);
	void *p;
	otlp_slab_stats_t st;
	int ok = 0;

	(void) seed;
	if (!s)
		return 0;
	/* Request 128 bytes from a slab with 32-byte slots → must malloc. */
	p = otlp_slab_alloc(s, 128);
	if (!p)
		goto out;
	memset(p, 0xAB, 128); /* writable across full range */
	otlp_slab_free_ptr(s, p);
	otlp_slab_get_stats(s, &st);
	ok = (st.slab_hits == 0 && st.malloc_fallbacks == 1 &&
		st.malloc_free_fallbacks == 1);
out:
	otlp_slab_free(s);
	return ok;
}

static int
prop_slab_overflow_fallback(uint64_t seed)
{
	otlp_slab_t *s = otlp_slab_create(64, 4);
	void *ptrs[6];
	otlp_slab_stats_t st;
	int ok = 0;

	(void) seed;
	if (!s)
		return 0;
	for (size_t i = 0; i < 6; i++)
	{
		ptrs[i] = otlp_slab_alloc(s, 64);
		if (!ptrs[i])
			goto out;
		memset(ptrs[i], (int) i, 64);
	}
	/* Verify content integrity (slots distinct from mallocs). */
	for (size_t i = 0; i < 6; i++)
	{
		uint8_t *p = ptrs[i];
		for (size_t j = 0; j < 64; j++)
		{
			if (p[j] != (uint8_t) i)
			{
				ok = 0;
				goto out;
			}
		}
	}
	for (size_t i = 0; i < 6; i++)
		otlp_slab_free_ptr(s, ptrs[i]);
	otlp_slab_get_stats(s, &st);
	ok = (st.slab_hits == 4 && st.malloc_fallbacks == 2 &&
		st.slab_free_hits == 4 && st.malloc_free_fallbacks == 2);
out:
	otlp_slab_free(s);
	return ok;
}

static int
prop_slab_free_routes_correctly(uint64_t seed)
{
	struct prng p;
	otlp_slab_t *s = otlp_slab_create(64, 4);
	void *arena_ptrs[4];
	void *malloc_ptrs[4];
	int ok = 0;

	(void) seed;
	if (!s)
		return 0;
	prng_seed(&p, seed);
	for (size_t i = 0; i < 4; i++)
		arena_ptrs[i] = otlp_slab_alloc(s, 64);
	for (size_t i = 0; i < 4; i++)
		malloc_ptrs[i] =
			otlp_slab_alloc(s, 128); /* oversize → malloc */
	for (size_t i = 0; i < 4; i++)
	{
		otlp_slab_free_ptr(s, arena_ptrs[i]);
		otlp_slab_free_ptr(s, malloc_ptrs[i]);
	}
	{
		otlp_slab_stats_t st;

		otlp_slab_get_stats(s, &st);
		ok = (st.slab_free_hits == 4 && st.malloc_free_fallbacks == 4 &&
			st.in_use == 0);
	}
	otlp_slab_free(s);
	return ok;
}

static int
prop_slab_stats_consistent(uint64_t seed)
{
	struct prng p;
	otlp_slab_t *s = otlp_slab_create(64, 8);
	otlp_slab_stats_t st;

	prng_seed(&p, seed);
	if (!s)
		return 0;
	for (size_t i = 0; i < 50; i++)
	{
		size_t sz = 1 + prng_u32(&p, 200);
		void *ptr = otlp_slab_alloc(s, sz);
		if (ptr)
			otlp_slab_free_ptr(s, ptr);
	}
	otlp_slab_get_stats(s, &st);
	otlp_slab_free(s);
	/* After balanced alloc/free, in_use must be 0. */
	return st.in_use == 0 && st.alloc_count == 50 && st.free_count == 50;
}

static int
prop_slab_install_global_allocator(uint64_t seed)
{
	otlp_slab_stats_t st;
	void *small = NULL;
	void *big = NULL;
	int ok = 0;

	(void) seed;
	/* Install slab: slot_size=64, capacity=8. */
	if (otlp_install_slab_allocator(64, 8) != OTLP_OK)
		return 0;
	/* Small alloc should hit the slab; big alloc should fall through. */
	small = otlp_malloc(32);
	big = otlp_malloc(256);
	if (!small || !big)
		goto out;
	memset(small, 0xAB, 32);
	memset(big, 0xCD, 256);
	otlp_free(small);
	small = NULL;
	otlp_free(big);
	big = NULL;
	ok = 1;
out:
	if (small)
		otlp_free(small);
	if (big)
		otlp_free(big);
	otlp_uninstall_slab_allocator();
	(void) st;
	return ok;
}

/* Regression (v0.5.51): double-free of an arena pointer must NOT
 * call libc free() (which is UB — the arena is owned by the slab).
 * The pre-v0.5.51 code fell through to free(ptr) on the path
 * "in arena but slot not marked used"; ASAN would flag this as
 * "calling free() on a non-heap pointer". */
static int
prop_slab_double_free_no_crash(uint64_t seed)
{
	otlp_slab_t *s;
	void *p;

	(void) seed;
	s = otlp_slab_create(64, 4);
	if (!s)
		return 0;
	p = otlp_slab_alloc(s, 16);
	if (!p)
	{
		otlp_slab_free(s);
		return 0;
	}
	otlp_slab_free_ptr(s, p);
	/* Second free of the same pointer: must be a silent no-op
	 * (slot is already not in use). Under ASAN, the pre-v0.5.51
	 * code would have flagged this as "free() on non-heap pointer". */
	otlp_slab_free_ptr(s, p);
	otlp_slab_free(s);
	return 1;
}

/* Regression (v0.5.85): otlp_realloc on an arena pointer. Any
 * arena-eligible buffer that later grows (e.g. the HTTP response
 * buffer: otlp_malloc(4096) then growth) reached the wrapped
 * allocator's pass-through realloc — libc realloc on a slab
 * pointer is undefined behavior. The hook must move the data out
 * of the slab. */
static int
prop_slab_global_realloc_growth(uint64_t seed)
{
	uint8_t *p;
	uint8_t *q;
	size_t i;
	int ok = 0;

	(void) seed;
	if (otlp_install_slab_allocator(8192, 64) != OTLP_OK)
		return 0;
	p = otlp_malloc(4096); /* arena-served: 4096 <= slot_size */
	if (!p)
		goto out;
	for (i = 0; i < 4096; i++)
		p[i] = (uint8_t)(i & 0xff);
	q = otlp_realloc(p, 8192); /* growth: must move out safely */
	if (!q)
		goto out_free;
	for (i = 0; i < 4096; i++)
		if (q[i] != (uint8_t)(i & 0xff))
			goto out_free;
	ok = 1;
out_free:
	otlp_free(q);
out:
	otlp_uninstall_slab_allocator();
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures +=
		property_run(prop_slab_roundtrip, "prop_slab_roundtrip", 5, 1);
	failures += property_run(
		prop_slab_slot_reuse, "prop_slab_slot_reuse", 5, 1);
	failures += property_run(prop_slab_oversize_fallback,
		"prop_slab_oversize_fallback",
		5,
		1);
	failures += property_run(prop_slab_overflow_fallback,
		"prop_slab_overflow_fallback",
		5,
		1);
	failures += property_run(prop_slab_free_routes_correctly,
		"prop_slab_free_routes_correctly",
		50,
		1);
	failures += property_run(prop_slab_stats_consistent,
		"prop_slab_stats_consistent",
		50,
		1);
	failures += property_run(prop_slab_global_realloc_growth,
		"prop_slab_global_realloc_growth",
		1,
		1);
	failures += property_run(prop_slab_install_global_allocator,
		"prop_slab_install_global_allocator",
		5,
		1);
	failures += property_run(prop_slab_double_free_no_crash,
		"prop_slab_double_free_no_crash",
		5,
		1);

	if (failures)
		printf("[property] %d slab property(ies) failed\n", failures);
	else
		printf("[property] all slab properties passed\n");
	return failures ? 1 : 0;
}
