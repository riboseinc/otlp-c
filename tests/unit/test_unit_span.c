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
static int
test_span_struct_size(void)
{
	/* 176 bytes at v0.5.76 (attrs, events, and links all
	 * grow-on-demand; the struct is just fixed fields + three
	 * (items, n, cap) triples). 512B catches any return to inline
	 * arrays. */
	assert(otlp_span_struct_size() <= 512);
	printf("[unit-span] sizeof(otlp_span)=%zu bytes\n",
		otlp_span_struct_size());
	return 0;
}

static int
test_span_create_free(void)
{
	otlp_span_t *span = otlp_span_create("test-span");

	assert(span != NULL);
	otlp_span_free(span);
	return 0;
}

static int
test_span_set_name(void)
{
	otlp_span_t *span = otlp_span_create("initial");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_name(span, "updated");

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int
test_span_attribute_string(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s =
		otlp_span_set_attribute_string(span, "user.id", "alice");

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int
test_span_attribute_int(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_attribute_int(span, "count", 42);

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int
test_span_attribute_double(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_attribute_double(span, "ratio", 3.14);

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int
test_span_attribute_bool(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_attribute_bool(span, "enabled", true);

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int
test_span_set_kind(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_kind(span, OTLP_SPAN_KIND_SERVER);

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int
test_span_set_status_ok(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_status(span, OTLP_STATUS_CODE_OK, NULL);

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int
test_span_set_status_error(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	otlp_status_t s = otlp_span_set_status(
		span, OTLP_STATUS_CODE_ERROR, "something failed");

	assert(s == OTLP_OK);
	otlp_span_free(span);
	return 0;
}

static int
test_span_many_attributes(void)
{
	otlp_span_t *span = otlp_span_create("stress");
	int i;
	char key[32];

	assert(span != NULL);
	for (i = 0; i < 128; i++)
	{
		snprintf(key, sizeof(key), "key%d", i);
		otlp_status_t s = otlp_span_set_attribute_int(span, key, i);

		assert(s == OTLP_OK);
	}
	otlp_span_free(span);
	return 0;
}

static int
test_tracer_start_span(void)
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

static int
test_span_attribute_upsert(void)
{
	otlp_span_t *span = otlp_span_create("test");
	const struct otlp_attribute *attrs;
	size_t n = 0;

	assert(span != NULL);
	assert(otlp_span_set_attribute_int(span, "k", 1) == OTLP_OK);
	assert(otlp_span_set_attribute_string(span, "other", "x") == OTLP_OK);
	assert(otlp_span_set_attribute_string(span, "k", "replaced") ==
		OTLP_OK);
	attrs = otlp_span_get_attrs(span, &n);
	assert(n == 2);
	assert(strcmp(attrs[0].key, "k") == 0);
	assert(attrs[0].type == OTLP_ATTR_STRING);
	assert(strcmp(attrs[0].v.string_val, "replaced") == 0);
	otlp_span_free(span);
	return 0;
}

static int
test_span_attribute_array_kvlist(void)
{
	const otlp_value_t items[3] = {
		{ .type = OTLP_VALUE_INT64, .v = { .int64_val = 7 } },
		{ .type = OTLP_VALUE_STRING, .v = { .string_val = "seven" } },
		{ .type = OTLP_VALUE_BOOL, .v = { .bool_val = true } },
	};
	const otlp_kv_t kvs[2] = {
		{ .key = "a",
			.value = { .type = OTLP_VALUE_INT64,
				.v = { .int64_val = 1 } } },
		{ .key = "b",
			.value = { .type = OTLP_VALUE_DOUBLE,
				.v = { .double_val = 2.5 } } },
	};
	otlp_span_t *span = otlp_span_create("test");
	otlp_span_t *clone;
	const struct otlp_attribute *attrs;
	size_t n = 0;

	assert(span != NULL);
	assert(otlp_span_set_attribute_array(span, "tags", items, 3) ==
		OTLP_OK);
	assert(otlp_span_set_attribute_kvlist(span, "map", kvs, 2) == OTLP_OK);
	attrs = otlp_span_get_attrs(span, &n);
	assert(n == 2);
	assert(attrs[0].type == OTLP_ATTR_ARRAY);
	assert(attrs[0].v.array_val->n == 3);
	assert(attrs[0].v.array_val->items[0].v.int64_val == 7);
	assert(strcmp(attrs[0].v.array_val->items[1].v.string_val, "seven") ==
		0);
	assert(attrs[0].v.array_val->items[2].v.bool_val == true);
	assert(attrs[1].type == OTLP_ATTR_KVLIST);
	assert(attrs[1].v.kvlist_val->n == 2);
	assert(strcmp(attrs[1].v.kvlist_val->entries[0].key, "a") == 0);
	assert(attrs[1].v.kvlist_val->entries[1].value.v.double_val == 2.5);

	/* Upsert over a composite: the old tree is released, the
	 * scalar takes its place. */
	assert(otlp_span_set_attribute_int(span, "tags", 1) == OTLP_OK);
	attrs = otlp_span_get_attrs(span, &n);
	assert(n == 2);
	assert(attrs[0].type == OTLP_ATTR_INT64);

	/* Clone deep-copies the kvlist tree. */
	clone = otlp_span_clone(span);
	assert(clone != NULL);
	attrs = otlp_span_get_attrs(clone, &n);
	assert(n == 2);
	assert(attrs[1].type == OTLP_ATTR_KVLIST);
	assert(attrs[1].v.kvlist_val != NULL);
	otlp_span_free(clone);
	otlp_span_free(span);
	return 0;
}

static int
test_event_typed_attributes(void)
{
	const uint8_t payload[2] = { 0xc0, 0xff };
	otlp_span_t *span = otlp_span_create("test");
	const struct otlp_event *ev;
	size_t n = 0;

	assert(span != NULL);
	assert(otlp_span_add_event(span, "e", 1) == OTLP_OK);
	assert(otlp_span_set_event_attribute_int(span, "attempt", 3) ==
		OTLP_OK);
	assert(otlp_span_set_event_attribute_double(span, "ratio", 0.5) ==
		OTLP_OK);
	assert(otlp_span_set_event_attribute_bool(span, "final", true) ==
		OTLP_OK);
	assert(otlp_span_set_event_attribute_bytes(span, "ctx", payload, 2) ==
		OTLP_OK);
	ev = otlp_span_get_events(span, &n);
	assert(n == 1 && ev[0].attrs.n == 4);
	assert(ev[0].attrs.items[0].type == OTLP_ATTR_INT64);
	assert(ev[0].attrs.items[0].v.int64_val == 3);
	assert(ev[0].attrs.items[1].type == OTLP_ATTR_DOUBLE);
	assert(ev[0].attrs.items[1].v.double_val == 0.5);
	assert(ev[0].attrs.items[2].type == OTLP_ATTR_BOOL);
	assert(ev[0].attrs.items[2].v.bool_val == true);
	assert(ev[0].attrs.items[3].type == OTLP_ATTR_BYTES);
	assert(ev[0].attrs.items[3].v.bytes_val.len == 2);
	assert(ev[0].attrs.items[3].v.bytes_val.data[1] == 0xff);
	otlp_span_free(span);
	return 0;
}

static int
test_link_typed_attributes(void)
{
	const uint8_t payload[2] = { 0x0a, 0x0b };
	uint8_t tid[OTLP_TRACE_ID_LEN] = { 1 };
	uint8_t sid[OTLP_SPAN_ID_LEN] = { 2 };
	otlp_span_t *span = otlp_span_create("test");
	const struct otlp_link *lk;
	size_t n = 0;

	assert(span != NULL);
	assert(otlp_span_add_link(span, tid, sid) == OTLP_OK);
	assert(otlp_span_set_link_attribute_int(span, "weight", 9) == OTLP_OK);
	assert(otlp_span_set_link_attribute_double(span, "score", 1.25) ==
		OTLP_OK);
	assert(otlp_span_set_link_attribute_bool(span, "cached", false) ==
		OTLP_OK);
	assert(otlp_span_set_link_attribute_bytes(span, "tag", payload, 2) ==
		OTLP_OK);
	lk = otlp_span_get_links(span, &n);
	assert(n == 1 && lk[0].attrs.n == 4);
	assert(lk[0].attrs.items[0].type == OTLP_ATTR_INT64);
	assert(lk[0].attrs.items[0].v.int64_val == 9);
	assert(lk[0].attrs.items[1].type == OTLP_ATTR_DOUBLE);
	assert(lk[0].attrs.items[1].v.double_val == 1.25);
	assert(lk[0].attrs.items[2].type == OTLP_ATTR_BOOL);
	assert(lk[0].attrs.items[2].v.bool_val == false);
	assert(lk[0].attrs.items[3].type == OTLP_ATTR_BYTES);
	assert(lk[0].attrs.items[3].v.bytes_val.len == 2);
	assert(lk[0].attrs.items[3].v.bytes_val.data[0] == 0x0a);
	otlp_span_free(span);
	return 0;
}

static int
test_event_link_overflow(void)
{
	uint8_t tid[OTLP_TRACE_ID_LEN] = { 1 };
	uint8_t sid[OTLP_SPAN_ID_LEN] = { 2 };
	otlp_span_t *span = otlp_span_create("test");
	int i;

	assert(span != NULL);
	for (i = 0; i < 64; i++)
		assert(otlp_span_add_event(span, "e", 1) == OTLP_OK);
	assert(otlp_span_add_event(span, "e", 1) == OTLP_ERR_OVERFLOW);
	for (i = 0; i < 64; i++)
		assert(otlp_span_add_link(span, tid, sid) == OTLP_OK);
	assert(otlp_span_add_link(span, tid, sid) == OTLP_ERR_OVERFLOW);
	otlp_span_free(span);
	return 0;
}

static int
test_event_link_attr_before_add(void)
{
	otlp_span_t *span = otlp_span_create("test");

	assert(span != NULL);
	assert(otlp_span_set_event_attribute_string(span, "k", "v") ==
		OTLP_ERR_INVALID_ARGUMENT);
	assert(otlp_span_set_link_attribute_string(span, "k", "v") ==
		OTLP_ERR_INVALID_ARGUMENT);
	assert(otlp_span_set_event_attribute_int(span, "k", 1) ==
		OTLP_ERR_INVALID_ARGUMENT);
	assert(otlp_span_set_link_attribute_bytes(span, "k", NULL, 0) ==
		OTLP_ERR_INVALID_ARGUMENT);
	otlp_span_free(span);
	return 0;
}

static int
test_tracer_start_child(void)
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

static int
test_tracer_multiple_spans_unique_ids(void)
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

int
main(void)
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
	failures += test_span_attribute_upsert();
	failures += test_span_attribute_array_kvlist();
	failures += test_tracer_start_span();
	failures += test_event_typed_attributes();
	failures += test_link_typed_attributes();
	failures += test_event_link_attr_before_add();
	failures += test_event_link_overflow();
	failures += test_tracer_start_child();
	failures += test_tracer_multiple_spans_unique_ids();

	if (failures)
		printf("[unit-span] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-span] PASS (20 tests)\n");

	return failures ? 1 : 0;
}
