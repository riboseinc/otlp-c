/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Fail-injecting allocator test.
 *
 * Iterates a "fail at Nth allocation" probe over an operation that
 * exercises the library's OOM cleanup paths. Each iteration:
 *   1. Sets fail_at = N.
 *   2. Resets alloc/free counters.
 *   3. Runs the operation.
 *   4. Frees any returned object.
 *   5. Asserts alloc_count == free_count (no leak).
 *
 * If a partial-init path fails to free something, the counts
 * diverge — the test fails. Under ASAN, double-frees and UB
 * also surface.
 *
 * Regression coverage:
 *   v0.5.47 — otlp_attribute_copy_all fail-path leak.
 *   v0.5.55 — otlp_exporter_create resource_attributes fail-path UB.
 */
#include <otlp-c/allocator.h>
#include <otlp-c/exporter.h>
#include <otlp-c/otlp.h>
#include <otlp-c/span.h>

#include "../src/span_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Fail-injecting allocator state ────────────────────────────
 *
 * alloc_count counts SUCCESSFUL allocs only. A failed alloc (one
 * that returns NULL) doesn't increment — there's nothing to free.
 * The leak check then asserts alloc_count == free_count after
 * each iteration, since every successful alloc must be paired
 * with a free (either via successful cleanup or via the
 * operation's destructor). */

static int alloc_count   = 0;
static int free_count    = 0;
static int fail_at       = -1;  /* -1: never fail; N: fail on Nth alloc */

static void *
fail_alloc(size_t n)
{
	/* Pre-check: would this alloc be the fail_at-th? If so, fail
	 * WITHOUT incrementing (no successful alloc → no free needed). */
	if (fail_at > 0 && alloc_count + 1 >= fail_at)
		return NULL;
	alloc_count++;
	return malloc(n);
}

static void *
fail_realloc(void *p, size_t n)
{
	if (fail_at > 0 && alloc_count + 1 >= fail_at)
		return NULL;
	alloc_count++;
	return realloc(p, n);
}

static void
fail_free(void *p)
{
	if (p)
		free_count++;
	free(p);
}

static const otlp_allocator_t fail_allocator = {
	.alloc   = fail_alloc,
	.realloc = fail_realloc,
	.free    = fail_free,
};

static void
reset_counters(int fail_at_value)
{
	alloc_count = 0;
	free_count  = 0;
	fail_at     = fail_at_value;
}

/* ── Test 1: exporter create with resource attributes ──────────
 *
 * Exercises the v0.5.55 fix path: resource_attributes array is
 * calloc'd; if any per-attribute dup fails, the fail-path iteration
 * must not touch uninitialized memory. Before v0.5.55, this would
 * have called otlp_free on garbage pointers under ASAN.
 */
static int
test_exporter_create_oom(void)
{
	otlp_resource_attr_t attrs[5];
	int		      i;
	int		      leaks = 0;
	int		      crashes = 0;

	for (i = 0; i < 5; i++) {
		attrs[i].key         = "key.x";
		attrs[i].value       = "value.x";
		attrs[i].type        = OTLP_RESOURCE_ATTR_STRING;
		attrs[i].int64_val   = 0;
		attrs[i].double_val  = 0.0;
		attrs[i].bool_val    = false;
	}
	/* Distinct keys/values so dup_str produces unique pointers. */
	attrs[0].key = "service.version"; attrs[0].value = "1.0.0";
	attrs[1].key = "host.name";       attrs[1].value = "host-001";
	attrs[2].key = "region";          attrs[2].value = "us-west-2";
	attrs[3].key = "instance.id";     attrs[3].value = "i-abc123";
	attrs[4].key = "runtime";         attrs[4].value = "go1.21";

	otlp_set_allocator(&fail_allocator);

	/* Probe fail_at from 1 to 60. Each iteration either succeeds
	 * (exporter returned, then freed) or fails partway (NULL
	 * returned, partial state cleaned up internally). */
	for (int n = 1; n <= 60; n++) {
		otlp_exporter_opts_t opts;
		otlp_exporter_t     *exp;

		memset(&opts, 0, sizeof(opts));
		opts.service_name        = "test";
		opts.resource_attributes = attrs;
		opts.n_resource_attributes = 5;

		reset_counters(n);
		exp = otlp_exporter_create(&opts);
		if (exp)
			otlp_exporter_free(exp);

		if (alloc_count != free_count) {
			printf("[oom] leak at fail_at=%d: alloc=%d free=%d\n",
			       n, alloc_count, free_count);
			leaks++;
		}
	}

	otlp_set_allocator(NULL);

	if (leaks > 0 || crashes > 0) {
		printf("[oom] exporter_create FAIL — %d leaks, %d crashes\n",
		       leaks, crashes);
		return 1;
	}
	printf("[oom] exporter_create PASS — 60 OOM iterations, "
	       "no leaks\n");
	return 0;
}

/* ── Test 2: span create + attribute copy ──────────────────────
 *
 * Exercises the v0.5.47 fix path: otlp_attribute_copy_all fail
 * cleanup. Indirect via otlp_span_clone — if alloc fails partway
 * through attribute copy, the partial state must be cleaned up.
 */
static int
test_span_clone_oom(void)
{
	int leaks = 0;

	otlp_set_allocator(&fail_allocator);

	for (int n = 1; n <= 50; n++) {
		otlp_span_t *span;
		otlp_span_t *clone;

		/* Build the source span with the default allocator first
		 * (so the source is always valid). */
		otlp_set_allocator(NULL);
		span = otlp_span_create("src");
		if (!span) {
			printf("[oom] span setup failed unexpectedly\n");
			return 1;
		}
		otlp_span_set_attribute_string(span, "k1", "v1");
		otlp_span_set_attribute_string(span, "k2", "v2");
		otlp_span_set_attribute_string(span, "k3", "v3");
		otlp_span_set_attribute_int(span, "n", 42);

		/* Now switch to the fail allocator and try the clone. */
		otlp_set_allocator(&fail_allocator);
		reset_counters(n);
		clone = otlp_span_clone(span);
		if (clone)
			otlp_span_free(clone);

		if (alloc_count != free_count) {
			printf("[oom] clone leak at fail_at=%d: alloc=%d free=%d\n",
			       n, alloc_count, free_count);
			leaks++;
		}

		/* Free the source under the default allocator. */
		otlp_set_allocator(NULL);
		otlp_span_free(span);
	}

	otlp_set_allocator(NULL);

	if (leaks > 0) {
		printf("[oom] span_clone FAIL — %d leaks\n", leaks);
		return 1;
	}
	printf("[oom] span_clone PASS — 50 OOM iterations, no leaks\n");
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_exporter_create_oom();
	failures += test_span_clone_oom();

	if (failures)
		printf("[oom] %d test(s) failed\n", failures);
	else
		printf("[oom] all OOM-injection tests passed\n");
	return failures ? 1 : 0;
}
