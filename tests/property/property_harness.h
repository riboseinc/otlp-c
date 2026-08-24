/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Property-based test harness. Each property is a function that
 * takes a seed and returns 1 if the invariant held, 0 if not. The
 * harness runs the property N times; if any run fails, it prints
 * the seed and exits non-zero.
 *
 * Failure mode: a crash (segfault, abort, trap) inside the
 * property is detected by CTest as a non-zero exit. The seed is
 * printed BEFORE each iteration so a crash leaves a trail.
 *
 * Usage:
 *
 *   static int my_prop(uint64_t seed) { ... }
 *
 *   int main(void) {
 *       int failures = 0;
 *       failures += property_run(my_prop, "my_prop", 1000, 1);
 *       return failures ? 1 : 0;
 *   }
 *
 * Env vars:
 *   OTLP_C_PROPERTY_SEED    — start from a specific seed (default 1).
 *   OTLP_C_PROPERTY_ITERS   — override iteration count.
 *   OTLP_C_PROPERTY_VERBOSE — print seed per iteration (debugging).
 *
 * Mirrors retrace's test/property/property_harness.h.
 */
#ifndef OTLP_C_TEST_PROPERTY_HARNESS_H
#define OTLP_C_TEST_PROPERTY_HARNESS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "prng.h"

#define PROPERTY_DEFAULT_ITERS 1000

/* Tiny string equality helper for properties. */
static inline int str_eq(const char *a, const char *b)
{
	return (a && b && strcmp(a, b) == 0) ? 1 : 0;
}

/*
 * Run a property `iters` times. Each iteration gets a deterministic
 * seed derived from base_seed + i. Returns the count of failures
 * (0 on success).
 */
static inline int property_run(int (*fn)(uint64_t seed),
			       const char *name,
			       unsigned long iters,
			       unsigned long base_seed)
{
	unsigned long env_iters;
	const char *env_iters_s;
	unsigned long env_seed;
	const char *env_seed_s;
	unsigned long n;
	unsigned long i;
	unsigned int fail = 0;

	env_seed_s = getenv("OTLP_C_PROPERTY_SEED");
	env_seed = (env_seed_s && *env_seed_s)
		? strtoul(env_seed_s, NULL, 0)
		: base_seed;

	env_iters_s = getenv("OTLP_C_PROPERTY_ITERS");
	env_iters = (env_iters_s && *env_iters_s)
		? strtoul(env_iters_s, NULL, 0)
		: 0;

	n = env_iters ? env_iters : iters;

	printf("[property] %s: %lu iterations, seed=%lu\n", name, n, env_seed);

	for (i = 0; i < n; i++) {
		uint64_t seed = env_seed + i;
		int held;

		if (getenv("OTLP_C_PROPERTY_VERBOSE"))
			printf("[property]   iter=%lu seed=%llu\n",
			       i, (unsigned long long)seed);

		held = fn(seed);
		if (!held) {
			printf("[property] %s: FAILED at iter=%lu seed=%llu\n",
			       name, i, (unsigned long long)seed);
			fail = 1;
			break;
		}
	}

	if (!fail)
		printf("[property] %s: PASS (%lu iters)\n", name, n);

	return fail ? 1 : 0;
}

#endif
