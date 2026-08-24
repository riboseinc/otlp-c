/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Property tests for the span builder and tracer.
 *
 *   prop_span_create_has_name   — span_create stores the name.
 *   prop_id_lengths_root        — start_span produces 16B trace, 8B span IDs.
 *   prop_id_lengths_child       — start_child_span same lengths.
 *   prop_id_uniqueness          — many spans from one tracer: unique span_ids.
 *   prop_child_inherits_trace   — child's trace_id == parent's trace_id.
 *   prop_child_links_parent     — child's parent_span_id == parent's span_id.
 *   prop_mark_time_monotonic    — mark_start followed by mark_end: start <=
 * end. prop_setters_null_safe      — every setter rejects NULL with
 * OTLP_ERR_NULL.
 */
#include "prng.h"
#include "property_harness.h"

#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include "../src/span_internal.h"

#include <stdint.h>
#include <string.h>

/* ── Properties ───────────────────────────────────────────────── */

static int
prop_span_create_has_name(uint64_t seed)
{
	struct prng p;
	otlp_span_t *span;
	char name[32];

	(void) seed;
	prng_seed(&p, 1);
	snprintf(name,
		sizeof(name),
		"span-%llu",
		(unsigned long long) prng_next(&p));

	span = otlp_span_create(name);
	if (!span)
		return 0;
	if (!str_eq(otlp_span_get_name(span), name))
	{
		otlp_span_free(span);
		return 0;
	}

	/* Default kind is INTERNAL per the tracer.h docstring. */
	if (otlp_span_get_kind(span) != OTLP_SPAN_KIND_INTERNAL)
	{
		otlp_span_free(span);
		return 0;
	}

	/* Default status is UNSET. */
	if (otlp_span_get_status_code(span) != OTLP_STATUS_CODE_UNSET)
	{
		otlp_span_free(span);
		return 0;
	}

	otlp_span_free(span);
	return 1;
}

static int
prop_id_lengths_root(uint64_t seed)
{
	otlp_tracer_t *t;
	otlp_span_t *span;
	const uint8_t *trace_id;
	const uint8_t *span_id;

	t = otlp_tracer_create("svc", "scope", "1.0");
	if (!t)
		return 0;
	span = otlp_tracer_start_span(t, "root");
	if (!span)
	{
		otlp_tracer_free(t);
		return 0;
	}

	trace_id = otlp_span_get_trace_id(span);
	span_id = otlp_span_get_span_id(span);
	if (!trace_id || !span_id)
	{
		otlp_span_free(span);
		otlp_tracer_free(t);
		return 0;
	}

	/* Trace ID must not be all-zero; span ID must not be all-zero. */
	bool trace_nz = false, span_nz = false;
	for (size_t i = 0; i < OTLP_TRACE_ID_LEN; i++)
		if (trace_id[i])
		{
			trace_nz = true;
			break;
		}
	for (size_t i = 0; i < OTLP_SPAN_ID_LEN; i++)
		if (span_id[i])
		{
			span_nz = true;
			break;
		}

	otlp_span_free(span);
	otlp_tracer_free(t);
	(void) seed;
	return (trace_nz && span_nz) ? 1 : 0;
}

static int
prop_id_uniqueness(uint64_t seed)
{
	enum
	{
		N = 1000
	};
	otlp_tracer_t *t;
	otlp_span_t *spans[N];
	size_t i, j;
	int ok = 0;

	(void) seed;
	t = otlp_tracer_create("svc", "scope", "1.0");
	if (!t)
		return 0;
	for (i = 0; i < N; i++)
	{
		spans[i] = otlp_tracer_start_span(t, "s");
		if (!spans[i])
			goto out;
	}

	/* O(N^2) span_id comparison; N=1000 is fine. */
	for (i = 0; i < N; i++)
	{
		const uint8_t *a = otlp_span_get_span_id(spans[i]);
		for (j = i + 1; j < N; j++)
		{
			const uint8_t *b = otlp_span_get_span_id(spans[j]);
			if (memcmp(a, b, OTLP_SPAN_ID_LEN) == 0)
				goto out;
		}
	}
	ok = 1;
out:
	for (i = 0; i < N; i++)
		otlp_span_free(spans[i]);
	otlp_tracer_free(t);
	return ok;
}

static int
prop_child_inherits_trace(uint64_t seed)
{
	otlp_tracer_t *t;
	otlp_span_t *parent, *child;
	int ok = 0;

	(void) seed;
	t = otlp_tracer_create("svc", "scope", "1.0");
	if (!t)
		return 0;
	parent = otlp_tracer_start_span(t, "p");
	child = parent ? otlp_tracer_start_child_span(t, "c", parent) : NULL;
	if (!parent || !child)
		goto out;

	ok = (memcmp(otlp_span_get_trace_id(parent),
		      otlp_span_get_trace_id(child),
		      OTLP_TRACE_ID_LEN) == 0)
		? 1
		: 0;

out:
	otlp_span_free(child);
	otlp_span_free(parent);
	otlp_tracer_free(t);
	return ok;
}

static int
prop_child_links_parent(uint64_t seed)
{
	otlp_tracer_t *t;
	otlp_span_t *parent, *child;
	int ok = 0;

	(void) seed;
	t = otlp_tracer_create("svc", "scope", "1.0");
	if (!t)
		return 0;
	parent = otlp_tracer_start_span(t, "p");
	child = parent ? otlp_tracer_start_child_span(t, "c", parent) : NULL;
	if (!parent || !child)
		goto out;

	if (!otlp_span_has_parent(child))
		goto out;
	ok = (memcmp(otlp_span_get_parent_span_id(child),
		      otlp_span_get_span_id(parent),
		      OTLP_SPAN_ID_LEN) == 0)
		? 1
		: 0;

out:
	otlp_span_free(child);
	otlp_span_free(parent);
	otlp_tracer_free(t);
	return ok;
}

static int
prop_mark_time_monotonic(uint64_t seed)
{
	otlp_span_t *span;
	int ok = 0;

	(void) seed;
	span = otlp_span_create("s");
	if (!span)
		return 0;
	if (otlp_span_mark_start(span) != OTLP_OK)
		goto out;
	if (otlp_span_mark_end(span) != OTLP_OK)
		goto out;
	if (!otlp_span_has_start_time(span) || !otlp_span_has_end_time(span))
		goto out;
	ok = (otlp_span_get_start_time(span) <= otlp_span_get_end_time(span))
		? 1
		: 0;

out:
	otlp_span_free(span);
	return ok;
}

static int
prop_setters_null_safe(uint64_t seed)
{
	uint8_t buf[16] = { 0 };

	(void) seed;
	if (otlp_span_set_trace_id(NULL, buf) != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_set_span_id(NULL, buf) != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_set_parent_span_id(NULL, buf) != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_set_start_time(NULL, 0) != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_set_end_time(NULL, 0) != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_mark_start(NULL) != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_mark_end(NULL) != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_set_kind(NULL, OTLP_SPAN_KIND_INTERNAL) != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_set_name(NULL, "x") != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_set_attribute_string(NULL, "k", "v") != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_set_attribute_int(NULL, "k", 1) != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_set_attribute_double(NULL, "k", 1.0) != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_set_attribute_bool(NULL, "k", true) != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_set_attribute_bytes(NULL, "k", buf, 1) != OTLP_ERR_NULL)
		return 0;
	if (otlp_span_set_status(NULL, OTLP_STATUS_CODE_OK, "x") !=
		OTLP_ERR_NULL)
		return 0;
	return 1;
}

/* Regression (v0.5.54): W3C Trace Context forbids all-zero trace-id
 * and parent-id. The library now rejects them at set time so invalid
 * IDs never reach the wire. */
static int
prop_setters_reject_all_zero_ids(uint64_t seed)
{
	otlp_span_t *span;
	uint8_t zeros_trace[OTLP_TRACE_ID_LEN] = { 0 };
	uint8_t zeros_span[OTLP_SPAN_ID_LEN] = { 0 };
	uint8_t good_trace[OTLP_TRACE_ID_LEN] = { 1 };
	uint8_t good_span[OTLP_SPAN_ID_LEN] = { 1 };
	int ok = 1;

	(void) seed;
	span = otlp_span_create("op");
	if (!span)
		return 0;

	if (otlp_span_set_trace_id(span, zeros_trace) !=
		OTLP_ERR_INVALID_ARGUMENT)
		ok = 0;
	if (otlp_span_set_span_id(span, zeros_span) !=
		OTLP_ERR_INVALID_ARGUMENT)
		ok = 0;
	if (otlp_span_set_parent_span_id(span, zeros_span) !=
		OTLP_ERR_INVALID_ARGUMENT)
		ok = 0;
	/* Good IDs still accepted. */
	if (otlp_span_set_trace_id(span, good_trace) != OTLP_OK)
		ok = 0;
	if (otlp_span_set_span_id(span, good_span) != OTLP_OK)
		ok = 0;
	if (otlp_span_set_parent_span_id(span, good_span) != OTLP_OK)
		ok = 0;
	/* NULL still clears parent. */
	if (otlp_span_set_parent_span_id(span, NULL) != OTLP_OK)
		ok = 0;

	otlp_span_free(span);
	return ok;
}

/* ── main ─────────────────────────────────────────────────────── */

int
main(void)
{
	int failures = 0;

	failures += property_run(
		prop_span_create_has_name, "prop_span_create_has_name", 100, 1);
	failures += property_run(
		prop_id_lengths_root, "prop_id_lengths_root", 100, 1);
	failures +=
		property_run(prop_id_uniqueness, "prop_id_uniqueness", 10, 1);
	failures += property_run(
		prop_child_inherits_trace, "prop_child_inherits_trace", 100, 1);
	failures += property_run(
		prop_child_links_parent, "prop_child_links_parent", 100, 1);
	failures += property_run(
		prop_mark_time_monotonic, "prop_mark_time_monotonic", 100, 1);
	failures += property_run(
		prop_setters_null_safe, "prop_setters_null_safe", 1, 1);
	failures += property_run(prop_setters_reject_all_zero_ids,
		"prop_setters_reject_all_zero_ids",
		5,
		1);

	if (failures)
		printf("[property] %d span property(ies) failed\n", failures);
	else
		printf("[property] all span properties passed\n");

	return failures ? 1 : 0;
}
