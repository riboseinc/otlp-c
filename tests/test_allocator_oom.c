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
#include <otlp-c/log.h>
#include <otlp-c/metric.h>
#include <otlp-c/otlp.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include "../src/log_internal.h"
#include "../src/metric_internal.h"
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

static int alloc_count = 0;
static int free_count = 0;
static int fail_at = -1; /* -1: never fail; N: fail on Nth alloc */

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
	/* Accounting model (v0.5.92): realloc(p!=NULL, n) is modeled
	 * as free(p) + alloc(n) — the old pointer is consumed by the
	 * libc realloc without a counted free otherwise, so any
	 * grow-on-demand pattern (the attribute vectors) showed a
	 * phantom +1 leak per growth. realloc(NULL, n) is alloc-only;
	 * a failed realloc leaves the old pointer alive (nothing
	 * counted). */
	if (fail_at > 0 && alloc_count + 1 >= fail_at)
		return NULL;
	alloc_count++;
	if (p)
		free_count++;
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
	.alloc = fail_alloc,
	.realloc = fail_realloc,
	.free = fail_free,
};

static void
reset_counters(int fail_at_value)
{
	alloc_count = 0;
	free_count = 0;
	fail_at = fail_at_value;
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
	int i;
	int leaks = 0;
	int crashes = 0;

	/* Distinct keys/values so dup_str produces unique pointers.
	 * v0.5.92 model: one key + one otlp_value_t. */
	attrs[0].key = "service.version";
	attrs[0].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "1.0.0" } };
	attrs[1].key = "host.name";
	attrs[1].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "host-001" } };
	attrs[2].key = "region";
	attrs[2].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "us-west-2" } };
	attrs[3].key = "instance.id";
	attrs[3].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "i-abc123" } };
	attrs[4].key = "runtime";
	attrs[4].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "go1.21" } };

	otlp_set_allocator(&fail_allocator);

	/* Probe fail_at from 1 to 60. Each iteration either succeeds
	 * (exporter returned, then freed) or fails partway (NULL
	 * returned, partial state cleaned up internally). */
	for (int n = 1; n <= 60; n++)
	{
		otlp_exporter_opts_t opts;
		otlp_exporter_t *exp;

		memset(&opts, 0, sizeof(opts));
		opts.service_name = "test";
		opts.resource_attributes = attrs;
		opts.n_resource_attributes = 5;

		reset_counters(n);
		exp = otlp_exporter_create(&opts);
		if (exp)
			otlp_exporter_free(exp);

		if (alloc_count != free_count)
		{
			printf("[oom] leak at fail_at=%d: alloc=%d free=%d\n",
				n,
				alloc_count,
				free_count);
			leaks++;
		}
	}

	otlp_set_allocator(NULL);

	if (leaks > 0 || crashes > 0)
	{
		printf("[oom] exporter_create FAIL — %d leaks, %d crashes\n",
			leaks,
			crashes);
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

	for (int n = 1; n <= 50; n++)
	{
		otlp_span_t *span;
		otlp_span_t *clone;

		/* Build the source span with the default allocator first
		 * (so the source is always valid). */
		otlp_set_allocator(NULL);
		span = otlp_span_create("src");
		if (!span)
		{
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

		if (alloc_count != free_count)
		{
			printf("[oom] clone leak at fail_at=%d: alloc=%d "
			       "free=%d\n",
				n,
				alloc_count,
				free_count);
			leaks++;
		}

		/* Free the source under the default allocator. */
		otlp_set_allocator(NULL);
		otlp_span_free(span);
	}

	otlp_set_allocator(NULL);

	if (leaks > 0)
	{
		printf("[oom] span_clone FAIL — %d leaks\n", leaks);
		return 1;
	}
	printf("[oom] span_clone PASS — 50 OOM iterations, no leaks\n");
	return 0;
}

/* ── Test 3: metric_create with histogram bounds ──────────────
 *
 * Exercises the histogram-specific allocation path in
 * otlp_metric_create: after the basic struct + 3 strings, the
 * histogram variant allocates bounds (malloc) + bucket_counts
 * (calloc). A failure between these two must clean up both. */
static int
test_metric_create_histogram_oom(void)
{
	double bounds[3] = { 1.0, 10.0, 100.0 };
	int leaks = 0;

	otlp_set_allocator(&fail_allocator);

	for (int n = 1; n <= 30; n++)
	{
		otlp_metric_t *m;

		reset_counters(n);
		m = otlp_metric_create(OTLP_METRIC_HISTOGRAM,
			"latency",
			"ms",
			"request latency",
			bounds,
			3);
		if (m)
			otlp_metric_free(m);

		if (alloc_count != free_count)
		{
			printf("[oom] metric_create leak at fail_at=%d: "
			       "alloc=%d free=%d\n",
				n,
				alloc_count,
				free_count);
			leaks++;
		}
	}

	otlp_set_allocator(NULL);

	if (leaks > 0)
	{
		printf("[oom] metric_create_histogram FAIL — %d leaks\n",
			leaks);
		return 1;
	}
	printf("[oom] metric_create_histogram PASS — 30 OOM iterations, "
	       "no leaks\n");
	return 0;
}

/* ── Test 4: metric_clone (full attribute + histogram + exp.hist) ──
 *
 * Exercises the most complex clone path: name/unit/description +
 * attrs + bounds/bucket_counts + exp_pos_counts + exp_neg_counts.
 * The fail path calls otlp_metric_free which must handle any
 * partially-initialized state. */
static int
test_metric_clone_oom(void)
{
	int leaks = 0;

	for (int n = 1; n <= 50; n++)
	{
		otlp_metric_t *src;
		otlp_metric_t *clone;
		double bounds[2] = { 1.0, 10.0 };
		uint64_t pos_counts[3] = { 1, 2, 3 };

		/* Build source under default allocator. */
		otlp_set_allocator(NULL);
		src = otlp_metric_create(
			OTLP_METRIC_EXP_HISTOGRAM, "src", "", "", bounds, 2);
		if (!src)
		{
			printf("[oom] metric_clone setup failed\n");
			return 1;
		}
		otlp_metric_set_attribute_string(src, "k1", "v1");
		otlp_metric_set_attribute_int(src, "k2", 42);
		if (otlp_metric_set_exp_histogram(
			    src, 20, 0, pos_counts, 3, 0, NULL, 0) != OTLP_OK)
		{
			otlp_metric_free(src);
			return 1;
		}

		/* Clone under fail allocator. */
		otlp_set_allocator(&fail_allocator);
		reset_counters(n);
		clone = otlp_metric_clone(src);
		if (clone)
			otlp_metric_free(clone);

		if (alloc_count != free_count)
		{
			printf("[oom] metric_clone leak at fail_at=%d: "
			       "alloc=%d free=%d\n",
				n,
				alloc_count,
				free_count);
			leaks++;
		}

		otlp_set_allocator(NULL);
		otlp_metric_free(src);
	}

	otlp_set_allocator(NULL);

	if (leaks > 0)
	{
		printf("[oom] metric_clone FAIL — %d leaks\n", leaks);
		return 1;
	}
	printf("[oom] metric_clone PASS — 50 OOM iterations, no leaks\n");
	return 0;
}

/* ── Test 5: log_record_clone (severity_text + body + attrs) ─── */
static int
test_log_record_clone_oom(void)
{
	int leaks = 0;

	for (int n = 1; n <= 40; n++)
	{
		otlp_log_record_t *src;
		otlp_log_record_t *clone;

		otlp_set_allocator(NULL);
		src = otlp_log_record_create(OTLP_SEVERITY_ERROR, "body");
		if (!src)
		{
			printf("[oom] log_clone setup failed\n");
			return 1;
		}
		otlp_log_record_set_severity_text(src, "ERROR");
		otlp_log_record_set_attribute_string(src, "k1", "v1");
		otlp_log_record_set_attribute_int(src, "k2", 99);

		otlp_set_allocator(&fail_allocator);
		reset_counters(n);
		clone = otlp_log_record_clone(src);
		if (clone)
			otlp_log_record_free(clone);

		if (alloc_count != free_count)
		{
			printf("[oom] log_clone leak at fail_at=%d: alloc=%d "
			       "free=%d\n",
				n,
				alloc_count,
				free_count);
			leaks++;
		}

		otlp_set_allocator(NULL);
		otlp_log_record_free(src);
	}

	otlp_set_allocator(NULL);

	if (leaks > 0)
	{
		printf("[oom] log_record_clone FAIL — %d leaks\n", leaks);
		return 1;
	}
	printf("[oom] log_record_clone PASS — 40 OOM iterations, "
	       "no leaks\n");
	return 0;
}

/* ── Test 6: tracer_create (3 string dups + struct) ─────────── */
static int
test_tracer_create_oom(void)
{
	int leaks = 0;

	otlp_set_allocator(&fail_allocator);

	for (int n = 1; n <= 20; n++)
	{
		otlp_tracer_t *t;

		reset_counters(n);
		t = otlp_tracer_create("svc", "scope", "1.0");
		if (t)
			otlp_tracer_free(t);

		if (alloc_count != free_count)
		{
			printf("[oom] tracer_create leak at fail_at=%d: "
			       "alloc=%d free=%d\n",
				n,
				alloc_count,
				free_count);
			leaks++;
		}
	}

	otlp_set_allocator(NULL);

	if (leaks > 0)
	{
		printf("[oom] tracer_create FAIL — %d leaks\n", leaks);
		return 1;
	}
	printf("[oom] tracer_create PASS — 20 OOM iterations, no leaks\n");
	return 0;
}

/* ── Test 7: flush_metric accounting under OOM ────────────────
 *
 * Regression for v0.5.59: on init or encode failure, flush_metric
 * must still update dropped_metrics_err to preserve the invariant
 * emitted_metrics == sent_metrics + dropped_metrics_err.
 *
 * Pre-v0.5.59 the init-failure path returned without updating
 * dropped_metrics_err, breaking the invariant. */
static int
test_flush_metric_oom_accounting(void)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp = NULL;
	otlp_metric_t *src;
	int bad_accounting = 0;

	/* Build source metric under default allocator. */
	src = otlp_metric_create(OTLP_METRIC_COUNTER, "m", "", "", NULL, 0);
	if (!src)
	{
		printf("[oom] flush_metric setup failed\n");
		return 1;
	}
	otlp_metric_record(src, 1.0);
	otlp_metric_mark_time(src);

	/* Create the exporter under the default allocator (so it
	 * definitely succeeds). */
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "test";
	otlp_set_allocator(NULL);
	exp = otlp_exporter_create(&opts);
	if (!exp)
	{
		otlp_metric_free(src);
		return 1;
	}
	otlp_exporter_set_null_transport(exp, true);

	/* Probe flush_metric under fail-injecting allocator. */
	otlp_set_allocator(&fail_allocator);
	for (int n = 1; n <= 15; n++)
	{
		otlp_exporter_stats_t stats;
		otlp_status_t st;

		reset_counters(n);
		st = otlp_exporter_flush_metric(exp, src);
		otlp_exporter_get_stats(exp, &stats);

		/* The invariant: emitted == sent + dropped_err.
		 * flush_metric increments emitted at start; either
		 * sent or dropped_err must follow. */
		if (stats.emitted_metrics !=
			stats.sent_metrics + stats.dropped_metrics_err)
		{
			printf("[oom] flush_metric accounting broken at "
			       "fail_at=%d: emitted=%llu sent=%llu "
			       "dropped=%llu (st=%d)\n",
				n,
				(unsigned long long) stats.emitted_metrics,
				(unsigned long long) stats.sent_metrics,
				(unsigned long long) stats.dropped_metrics_err,
				(int) st);
			bad_accounting++;
		}
	}

	otlp_set_allocator(NULL);
	otlp_metric_free(src);
	otlp_exporter_free(exp);

	if (bad_accounting > 0)
	{
		printf("[oom] flush_metric FAIL — %d accounting breaks\n",
			bad_accounting);
		return 1;
	}
	printf("[oom] flush_metric PASS — 15 OOM iterations, "
	       "accounting invariant holds\n");
	return 0;
}

/* ── Test 8: flush_log accounting under OOM ─────────────────── */
static int
test_flush_log_oom_accounting(void)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp = NULL;
	otlp_log_record_t *src;
	int bad_accounting = 0;

	src = otlp_log_record_create(OTLP_SEVERITY_INFO, "body");
	if (!src)
	{
		printf("[oom] flush_log setup failed\n");
		return 1;
	}

	memset(&opts, 0, sizeof(opts));
	opts.service_name = "test";
	otlp_set_allocator(NULL);
	exp = otlp_exporter_create(&opts);
	if (!exp)
	{
		otlp_log_record_free(src);
		return 1;
	}
	otlp_exporter_set_null_transport(exp, true);

	otlp_set_allocator(&fail_allocator);
	for (int n = 1; n <= 15; n++)
	{
		otlp_exporter_stats_t stats;
		otlp_status_t st;

		reset_counters(n);
		st = otlp_exporter_flush_log(exp, src);
		otlp_exporter_get_stats(exp, &stats);

		if (stats.emitted_logs !=
			stats.sent_logs + stats.dropped_logs_err)
		{
			printf("[oom] flush_log accounting broken at "
			       "fail_at=%d: emitted=%llu sent=%llu "
			       "dropped=%llu (st=%d)\n",
				n,
				(unsigned long long) stats.emitted_logs,
				(unsigned long long) stats.sent_logs,
				(unsigned long long) stats.dropped_logs_err,
				(int) st);
			bad_accounting++;
		}
	}

	otlp_set_allocator(NULL);
	otlp_log_record_free(src);
	otlp_exporter_free(exp);

	if (bad_accounting > 0)
	{
		printf("[oom] flush_log FAIL — %d accounting breaks\n",
			bad_accounting);
		return 1;
	}
	printf("[oom] flush_log PASS — 15 OOM iterations, "
	       "accounting invariant holds\n");
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_exporter_create_oom();
	failures += test_span_clone_oom();
	failures += test_metric_create_histogram_oom();
	failures += test_metric_clone_oom();
	failures += test_log_record_clone_oom();
	failures += test_tracer_create_oom();
	failures += test_flush_metric_oom_accounting();
	failures += test_flush_log_oom_accounting();

	if (failures)
		printf("[oom] %d test(s) failed\n", failures);
	else
		printf("[oom] all OOM-injection tests passed\n");
	return failures ? 1 : 0;
}
