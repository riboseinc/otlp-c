/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Microbenchmark: slab allocator vs malloc for small allocations.
 *
 *   cmake -B build -DOTLP_C_BUILD_BENCH=ON
 *   cmake --build build
 *   ./build/bench/otlp_bench_slab
 *
 * Measures: 100K alloc+free cycles of 64-byte objects via (a) system
 * malloc, (b) otlp-c slab allocator. Reports ns/op and speedup ratio.
 */
#include <otlp-c/slab.h>

#include "../src/internal_util.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N_ALLOCS 100000
#define SLOT_SIZE 64
#define CAPACITY  256

static uint64_t
now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int
main(void)
{
	void  **ptrs;
	uint64_t t_malloc, t_slab;
	double  ns_malloc, ns_slab;

	ptrs = malloc(N_ALLOCS * sizeof(void *));
	if (!ptrs) {
		fprintf(stderr, "alloc failed\n");
		return 1;
	}

	/* ── System malloc ─────────────────────────────────────────── */
	t_malloc = now_ns();
	for (int i = 0; i < N_ALLOCS; i++) {
		ptrs[i] = malloc(SLOT_SIZE);
		if (!ptrs[i]) {
			fprintf(stderr, "malloc failed at %d\n", i);
			return 1;
		}
	}
	for (int i = 0; i < N_ALLOCS; i++)
		free(ptrs[i]);
	t_malloc = now_ns() - t_malloc;

	/* ── Slab allocator ────────────────────────────────────────── */
	otlp_install_slab_allocator(SLOT_SIZE, CAPACITY);

	t_slab = now_ns();
	for (int i = 0; i < N_ALLOCS; i++) {
		ptrs[i] = otlp_malloc(SLOT_SIZE);
		if (!ptrs[i]) {
			fprintf(stderr, "slab alloc failed at %d\n", i);
			break;
		}
	}
	for (int i = 0; i < N_ALLOCS; i++)
		otlp_free(ptrs[i]);
	t_slab = now_ns() - t_slab;

	otlp_uninstall_slab_allocator();

	ns_malloc = (double)t_malloc / N_ALLOCS;
	ns_slab   = (double)t_slab / N_ALLOCS;

	printf("slab benchmark: %d alloc+free cycles of %d-byte objects\n",
	       N_ALLOCS, SLOT_SIZE);
	printf("  malloc:  %.1f ns/op  (%" PRIu64 " ms total)\n",
	       ns_malloc, t_malloc / 1000000);
	printf("  slab:    %.1f ns/op  (%" PRIu64 " ms total)\n",
	       ns_slab, t_slab / 1000000);
	printf("  speedup: %.2fx\n", ns_malloc / ns_slab);

	free(ptrs);
	return 0;
}
