/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Property tests for the retry timing policy (v0.6.13).
 *
 * The invariants that were previously only exercised end-to-end
 * through exporter scenarios, now tested directly against the
 * pure decision functions:
 *
 *   prop_jitter_bounds     — a drawn delay is always within
 *                            [0, min(initial << (attempt-1), max)]
 *   prop_shift_cap         — huge attempt values saturate at max,
 *                            with no undefined shift (UBSAN pins it)
 *   prop_retry_after_floor — a server floor never lowers the delay
 *                            below min(floor, max), the cap always
 *                            holds, and server_driven is exactly
 *                            "the floor won"
 *   prop_jitter_nonzero    — xorshift64s from a nonzero state never
 *                            yields zero and is deterministic
 */
#include "prng.h"
#include "../test_util.h"
#include "property_harness.h"

#include "../src/retry_policy.h"

#include <stdint.h>

static int
prop_jitter_bounds(uint64_t seed)
{
	struct prng p;
	struct otlp_retry_cfg cfg;
	uint32_t attempt;
	int i;

	prng_seed(&p, seed);
	for (i = 0; i < 200; i++)
	{
		uint32_t initial = 1 + prng_u32(&p, 10000);
		uint32_t max = initial + prng_u32(&p, 90000);
		uint32_t base;
		uint32_t shift;
		uint64_t expected_cap;

		cfg.initial_ms = initial;
		cfg.max_ms = max;
		attempt = 1 + prng_u32(&p, 200);

		base = otlp_retry_base_delay_ms(attempt, &cfg);
		shift = attempt - 1;
		if (shift > 31)
			shift = 31;
		expected_cap = (uint64_t) initial << shift;
		if (expected_cap > max)
			expected_cap = max;
		check_true(base <= expected_cap);
	}
	return 1;
}

static int
prop_shift_cap(uint64_t seed)
{
	struct prng p;
	struct otlp_retry_cfg cfg = { 100, 5000 };

	(void) seed;
	prng_seed(&p, 1);
	/* attempt = UINT32_MAX: shift would be 2^32-2 without the
	 * clamp — UB on a 32-bit shift. Must saturate at max. */
	check_true(otlp_retry_base_delay_ms(1, &cfg) == 100);
	check_true(otlp_retry_base_delay_ms(33, &cfg) == 5000);
	check_true(otlp_retry_base_delay_ms(UINT32_MAX, &cfg) == 5000);
	return 1;
}

static int
prop_retry_after_floor(uint64_t seed)
{
	struct prng p;
	int i;

	prng_seed(&p, seed);
	for (i = 0; i < 300; i++)
	{
		struct otlp_retry_cfg cfg;
		uint32_t attempt = 1 + prng_u32(&p, 40);
		uint32_t floor = prng_u32(&p, 200000);
		uint64_t prng_state = prng_next(&p) | 1;
		uint64_t prng_copy = prng_state;
		bool server_driven = false;
		uint32_t delay;
		uint32_t redraw;

		cfg.initial_ms = 1 + prng_u32(&p, 5000);
		cfg.max_ms = cfg.initial_ms + prng_u32(&p, 60000);

		delay = otlp_retry_delay_ms(
			&prng_state, attempt, floor, &cfg, &server_driven);

		/* The cap always holds. */
		check_true(delay <= cfg.max_ms);
		/* The floor never lowers the delay below min(floor, max)
		 * — the Retry-After contract (RFC 7231 §7.1.3). */
		check_true(delay >= (floor < cfg.max_ms ? floor : cfg.max_ms));
		/* server_driven is exactly "the floor beat the drawn
		 * jitter": recompute the draw from the saved state. */
		{
			uint32_t base = otlp_retry_base_delay_ms(attempt, &cfg);
			uint64_t drawn;

			check_true(base > 0); /* max >= initial >= 1 */
			drawn = (uint32_t)(otlp_jitter_next(&prng_copy) %
				((uint64_t) base + 1));
			check_true(server_driven == (floor > drawn));
			/* And the returned delay is the contract applied
			 * to that draw. */
			redraw = (uint32_t) drawn;
			if (floor > redraw)
				redraw = floor;
			if (redraw > cfg.max_ms)
				redraw = cfg.max_ms;
			check_true(delay == redraw);
		}
	}
	return 1;
}

static int
prop_jitter_nonzero(uint64_t seed)
{
	struct prng p;
	uint64_t state = seed | 1; /* xorshift needs nonzero */
	int i;

	prng_seed(&p, seed);
	for (i = 0; i < 1000; i++)
	{
		uint64_t a = otlp_jitter_next(&state);
		uint64_t b;

		check_true(a != 0);
		check_true(state != 0);
		/* Deterministic: replaying the same state reproduces. */
		b = state;
		check_true(otlp_jitter_next(&b) == otlp_jitter_next(&state));
	}
	return 1;
}

int
main(void)
{
	int failures = 0;

	failures +=
		property_run(prop_jitter_bounds, "prop_jitter_bounds", 100, 1);
	failures += property_run(prop_shift_cap, "prop_shift_cap", 10, 1);
	failures += property_run(
		prop_retry_after_floor, "prop_retry_after_floor", 100, 1);
	failures +=
		property_run(prop_jitter_nonzero, "prop_jitter_nonzero", 50, 1);

	if (failures)
		printf("[property] %d retry-policy property(ies) failed\n",
			failures);
	else
		printf("[property] all retry-policy properties passed\n");
	return failures ? 1 : 0;
}
