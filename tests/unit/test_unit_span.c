// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the span lifecycle. Complements the property tests
// with specific known-answer assertions for each public setter.

#include "../../src/span_internal.h"

#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <assert.h>
#include <string.h>
#include <stdio.h>

/* Regression guard (v0.5.68): sizeof(struct otlp_span) must stay
 * small. Pre-v0.5.68 the embedded events[64]/links[64] arrays each
 * carried inline attrs[32] arrays, making the struct ~139KB —
 * every span create/clone allocated and zeroed 139KB, dominating
 * the emit path (~30μs/span; ~650K spans/s after the fix).
 * Event/link attrs are now lazily heap-allocated pointers. If a
 * change pushes the struct past this budget, re-measure the emit
 * benchmark before accepting it. */
static int test_span_struct_size(void)
{
	/* 8.8KB at v0.5.68 (span attrs inline: 4KB; event/link
	 * headers: ~4.6KB). 16KB is a generous ceiling that still
	 * catches a return to inline-array growth. */
	assert(otlp_span_struct_size() <= 16 * 1024);
	printf("[unit-span] sizeof(otlp_span)=%zu bytes\n",
	       otlp_span_struct_size());
	return 0;
}

static int test_span_create_free(void)
{
	otlp_span_t *span = otlp_span_create("test-span");

	assert(span != NULL);
	otlp_span_free(span);
	return 0;
}

static int test_span_set_name(void)
{
	otlp_span_t *span = otlp_span_create("initial");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_name(span, "updated");

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int test_span_attribute_string(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_attribute_string(span, "user.id", "alice");

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int test_span_attribute_int(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_attribute_int(span, "count", 42);

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int test_span_attribute_double(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_attribute_double(span, "ratio", 3.14);

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int test_span_attribute_bool(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_attribute_bool(span, "enabled", true);

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int test_span_set_kind(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_kind(span, OTLP_SPAN_KIND_SERVER);

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int test_span_set_status_ok(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_status(span, OTLP_STATUS_CODE_OK, NULL);

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int test_span_set_status_error(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_status(span, OTLP_STATUS_CODE_ERROR,
					       "something failed");

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int test_span_many_attributes(void)
{
	otlp_span_t *span = otlp_span_create("stress");
	int i;
	char key[32];

	assert(span != NULL);
	for (i = 0; i < 128; i++) {
		snprintf(key, sizeof(key), "key%d", i);
		otlp_status_t s = otlp_span_set_attribute_int(span, key, i);

		assert(s == OTLP_OK);
	}
	otlp_span_free(span);
	return 0;
}

static int test_tracer_start_span(void)
{
	otlp_tracer_t *tracer = otlp_tracer_create("svc", "lib", "0.1.0");
	otlp_span_t *span;

	assert(tracer != NULL);
	span = otlp_tracer_start_span(tracer, "op");
	assert(span != NULL);
	otlp_span_free(span);
	otlp_tracer_free(tracer);
	return 0;
}

static int test_tracer_start_child(void)
{
	otlp_tracer_t *tracer = otlp_tracer_create("svc", "lib", "0.1.0");
	otlp_span_t *parent;
	otlp_span_t *child;

	assert(tracer != NULL);
	parent = otlp_tracer_start_span(tracer, "parent");
	assert(parent != NULL);
	child = otlp_tracer_start_child_span(tracer, "child", parent);
	assert(child != NULL);
	otlp_span_free(child);
	otlp_span_free(parent);
	otlp_tracer_free(tracer);
	return 0;
}

static int test_tracer_multiple_spans_unique_ids(void)
{
	otlp_tracer_t *tracer = otlp_tracer_create("svc", "lib", "0.1.0");
	otlp_span_t *s1;
	otlp_span_t *s2;

	assert(tracer != NULL);
	s1 = otlp_tracer_start_span(tracer, "span1");
	s2 = otlp_tracer_start_span(tracer, "span2");
	assert(s1 != s2);
	assert(s1 != NULL);
	assert(s2 != NULL);
	/* In practice each span has a unique random ID; the opaque API
	 * doesn't let us inspect IDs directly, but we can verify the
	 * span pointers are distinct. */
	otlp_span_free(s1);
	otlp_span_free(s2);
	otlp_tracer_free(tracer);
	return 0;
}

int main(void)
{
	int failures = 0;

	failures += test_span_struct_size();
	failures += test_span_create_free();
	failures += test_span_set_name();
	failures += test_span_attribute_string();
	failures += test_span_attribute_int();
	failures += test_span_attribute_double();
	failures += test_span_attribute_bool();
	failures += test_span_set_kind();
	failures += test_span_set_status_ok();
	failures += test_span_set_status_error();
	failures += test_span_many_attributes();
	failures += test_tracer_start_span();
	failures += test_tracer_start_child();
	failures += test_tracer_multiple_spans_unique_ids();

	if (failures)
		printf("[unit-span] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-span] PASS (14 tests)\n");

	return failures ? 1 : 0;
}
