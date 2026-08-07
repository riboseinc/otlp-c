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
 */
#include "prng.h"
#include "property_harness.h"

#include <otlp-c/slab.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static int
prop_slab_roundtrip(uint64_t seed)
{
	struct prng    p;
	otlp_slab_t   *s = otlp_slab_create(64, 8);
	void	      *ptrs[8];
	otlp_slab_stats_t st;

	(void) seed;
	if (!s)
		return 0;
	prng_seed(&p, 0);
	for (size_t i = 0; i < 8; i++) {
		ptrs[i] = otlp_slab_alloc(s, 32);
		if (!ptrs[i]) {
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
	otlp_slab_t   *s = otlp_slab_create(64, 8);
	void	      *p1, *p2;
	int	       ok = 0;

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
	otlp_slab_t   *s = otlp_slab_create(32, 8);
	void	      *p;
	otlp_slab_stats_t st;
	int	       ok = 0;

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
	otlp_slab_t   *s = otlp_slab_create(64, 4);
	void	      *ptrs[6];
	otlp_slab_stats_t st;
	int	       ok = 0;

	(void) seed;
	if (!s)
		return 0;
	for (size_t i = 0; i < 6; i++) {
		ptrs[i] = otlp_slab_alloc(s, 64);
		if (!ptrs[i])
			goto out;
		memset(ptrs[i], (int) i, 64);
	}
	/* Verify content integrity (slots distinct from mallocs). */
	for (size_t i = 0; i < 6; i++) {
		uint8_t *p = ptrs[i];
		for (size_t j = 0; j < 64; j++) {
			if (p[j] != (uint8_t) i) {
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
	struct prng    p;
	otlp_slab_t   *s = otlp_slab_create(64, 4);
	void	      *arena_ptrs[4];
	void	      *malloc_ptrs[4];
	int	       ok = 0;

	(void) seed;
	if (!s)
		return 0;
	prng_seed(&p, seed);
	for (size_t i = 0; i < 4; i++)
		arena_ptrs[i] = otlp_slab_alloc(s, 64);
	for (size_t i = 0; i < 4; i++)
		malloc_ptrs[i] = otlp_slab_alloc(s, 128); /* oversize → malloc */
	for (size_t i = 0; i < 4; i++) {
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
	struct prng    p;
	otlp_slab_t   *s = otlp_slab_create(64, 8);
	otlp_slab_stats_t st;

	prng_seed(&p, seed);
	if (!s)
		return 0;
	for (size_t i = 0; i < 50; i++) {
		size_t sz  = 1 + prng_u32(&p, 200);
		void  *ptr = otlp_slab_alloc(s, sz);
		if (ptr)
			otlp_slab_free_ptr(s, ptr);
	}
	otlp_slab_get_stats(s, &st);
	otlp_slab_free(s);
	/* After balanced alloc/free, in_use must be 0. */
	return st.in_use == 0 && st.alloc_count == 50 && st.free_count == 50;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_slab_roundtrip,
				 "prop_slab_roundtrip", 5, 1);
	failures += property_run(prop_slab_slot_reuse,
				 "prop_slab_slot_reuse", 5, 1);
	failures += property_run(prop_slab_oversize_fallback,
				 "prop_slab_oversize_fallback", 5, 1);
	failures += property_run(prop_slab_overflow_fallback,
				 "prop_slab_overflow_fallback", 5, 1);
	failures += property_run(prop_slab_free_routes_correctly,
				 "prop_slab_free_routes_correctly", 50, 1);
	failures += property_run(prop_slab_stats_consistent,
				 "prop_slab_stats_consistent", 50, 1);

	if (failures)
		printf("[property] %d slab property(ies) failed\n", failures);
	else
		printf("[property] all slab properties passed\n");
	return failures ? 1 : 0;
}
