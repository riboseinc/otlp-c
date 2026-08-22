/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Shared test utilities.
 *
 * check_ok()/check_true() are the always-evaluated versions of
 * `st = f(); assert(st == OTLP_OK);`. Passing the expression as an
 * ARGUMENT means it executes even in Release builds (NDEBUG elides
 * assert()'s body, not the call), so:
 *   - the test can never pass vacuously under Release (v0.5.82-95
 *     lesson: calls inside assert() vanish and stats stay 0), and
 *   - the result is consumed in both configurations, so Release
 *     builds stay zero-warning (-Wunused-but-set-variable).
 *
 * Keep new test code on these helpers for every result that is
 * checked once and never read again.
 */
#ifndef OTLP_C_TEST_UTIL_H
#define OTLP_C_TEST_UTIL_H

#include <otlp-c/status.h>

#include <assert.h>

static inline void
check_true(int cond)
{
	assert(cond);
	(void) cond;
}

static inline void
check_ok(otlp_status_t st)
{
	assert(st == OTLP_OK);
	(void) st;
}

#endif
