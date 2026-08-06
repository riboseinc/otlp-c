/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Test: custom allocator hook.
 * Verifies that otlp_set_allocator() routes all internal
 * allocations through the caller's functions.
 */
#include <otlp-c/allocator.h>
#include <otlp-c/otlp.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int alloc_count = 0;
static int free_count  = 0;
static int realloc_count = 0;

static void *
counting_alloc(size_t n)
{
	alloc_count++;
	return malloc(n);
}

static void *
counting_realloc(void *p, size_t n)
{
	realloc_count++;
	return realloc(p, n);
}

static void
counting_free(void *p)
{
	if (p)
		free_count++;
	free(p);
}

int
main(void)
{
	otlp_allocator_t custom = {
		.alloc   = counting_alloc,
		.realloc = counting_realloc,
		.free    = counting_free,
	};

	/* Install custom allocator BEFORE any otlp-c call. */
	otlp_set_allocator(&custom);

	/* Create a span — should go through counting_alloc. */
	otlp_span_t *span = otlp_span_create("test-span");
	assert(span != NULL);
	otlp_span_set_attribute_string(span, "key", "value");
	otlp_span_mark_end(span);

	/* Verify allocations happened through the custom allocator. */
	printf("[allocator] alloc_count=%d free_count=%d "
	       "realloc_count=%d\n",
		alloc_count, free_count, realloc_count);
	assert(alloc_count > 0);

	/* Free the span — should go through counting_free. */
	otlp_span_free(span);
	assert(free_count > 0);

	/* Reset to defaults. */
	otlp_set_allocator(NULL);

	/* Verify default allocator works. */
	otlp_span_t *s2 = otlp_span_create("default-alloc");
	assert(s2 != NULL);
	otlp_span_free(s2);

	printf("[allocator] PASS — custom allocator intercepted "
	       "%d allocs, %d frees\n", alloc_count, free_count);
	return 0;
}
