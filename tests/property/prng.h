/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property-based test PRNG. xorshift64 — deterministic from a seed,
 * tiny (4 lines of state), good statistical properties for testing.
 *
 * Default seed is fixed (1) so test runs are reproducible. Override
 * via the OTLP_C_PROPERTY_SEED env var when chasing a specific
 * failure.
 *
 * Mirrors retrace's test/property/prng.h.
 */
#ifndef OTLP_C_TEST_PROPERTY_PRNG_H
#define OTLP_C_TEST_PROPERTY_PRNG_H

#include <stdint.h>

struct prng {
	uint64_t state;
};

static inline void prng_seed(struct prng *p, uint64_t seed)
{
	p->state = seed ? seed : 0x9E3779B97F4A7C15ULL;
}

static inline uint64_t prng_next(struct prng *p)
{
	uint64_t x = p->state;

	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;

	p->state = x;
	return x;
}

static inline uint32_t prng_u32(struct prng *p, uint32_t bound_exclusive)
{
	if (bound_exclusive == 0)
		return 0;
	return (uint32_t)(prng_next(p) % bound_exclusive);
}

#endif
