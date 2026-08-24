/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Shared test utilities.
 *
 * check_ok()/check_true() are the always-ENFORCED versions of
 * `st = f(); assert(st == OTLP_OK);`. Passing the expression as an
 * ARGUMENT means it executes even in Release builds, and the
 * failure path aborts in every configuration — NDEBUG elides
 * assert()'s body, not this call:
 *   - the test can never pass vacuously under Release (v0.5.82-95
 *     lesson: calls inside assert() vanish and stats stay 0), and
 *     a false check FAILS under Release too (v0.5.99: v0.5.98's
 *     first cut only evaluated the expression — a false check was
 *     silently discarded, so Release runs executed the checks
 *     without enforcing them), and
 *   - the result is consumed in both configurations, so Release
 *     builds stay zero-warning (-Wunused-but-set-variable).
 *
 * Keep new test code on these helpers for every result that is
 * checked once and never read again.
 */
#ifndef OTLP_C_TEST_UTIL_H
#define OTLP_C_TEST_UTIL_H

#include <otlp-c/status.h>

#include <stdio.h>
#include <stdlib.h>

static inline void
check_true_at(const char *file, int line, int cond)
{
	if (!cond)
	{
		fprintf(stderr, "check_true FAILED at %s:%d\n", file, line);
		abort();
	}
}

static inline void
check_ok_at(const char *file, int line, otlp_status_t st)
{
	if (st != OTLP_OK)
	{
		fprintf(stderr,
			"check_ok FAILED at %s:%d: st=%d\n",
			file,
			line,
			(int) st);
		abort();
	}
}

/* Single-expression macros (no control flow): they only pin the
 * failure report to the CALL site; the logic lives in the inline
 * functions above. */
#define check_true(cond) (check_true_at(__FILE__, __LINE__, (cond)))
#define check_ok(st) (check_ok_at(__FILE__, __LINE__, (st)))

#endif
