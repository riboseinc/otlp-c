// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the metric lifecycle. Complements the property
// tests with known-answer assertions for each public setter and
// the lazy attribute-array contract.

#include "../../src/metric_internal.h"

#include <otlp-c/metric.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Regression guard (v0.5.69): sizeof(struct otlp_metric) must stay
 * small. Pre-v0.5.69 the inline attrs[128] array made the struct
 * 4312 bytes — 95% of every metric allocation was the attribute
 * array, zeroed on every create/clone even though most metrics
 * carry no attributes. Attrs are now a lazily heap-allocated
 * pointer. If a change pushes the struct past this budget,
 * re-measure before accepting it. */
static int
test_metric_struct_size(void)
{
	/* 224 bytes at v0.5.69. 1KB is a generous ceiling that still
	 * catches a return to the inline-array layout. */
	assert(otlp_metric_struct_size() <= 1024);
	printf("[unit-metric] sizeof(otlp_metric)=%zu bytes\n",
		otlp_metric_struct_size());
	return 0;
}

static int
test_metric_create_free(void)
{
	otlp_metric_t *m = otlp_metric_create(
		OTLP_METRIC_COUNTER, "requests", "1", "doc", NULL, 0);

	assert(m != NULL);
	otlp_metric_free(m);
	return 0;
}

static int
test_metric_no_attrs_lazy(void)
{
	otlp_metric_t *m = otlp_metric_create(
		OTLP_METRIC_COUNTER, "requests", "1", "doc", NULL, 0);
	size_t n = 99;
	const struct otlp_attribute *attrs;

	assert(m != NULL);
	attrs = otlp_metric_get_attrs(m, &n);
	assert(attrs == NULL);
	assert(n == 0);
	otlp_metric_free(m);
	return 0;
}

static int
test_metric_attribute_string(void)
{
	otlp_metric_t *m = otlp_metric_create(
		OTLP_METRIC_COUNTER, "requests", "1", "doc", NULL, 0);
	const struct otlp_attribute *attrs;
	size_t n = 0;

	assert(m != NULL);
	assert(otlp_metric_set_attribute_string(m, "host", "a1") == OTLP_OK);
	attrs = otlp_metric_get_attrs(m, &n);
	assert(attrs != NULL && n == 1);
	assert(strcmp(attrs[0].key, "host") == 0);
	assert(attrs[0].type == OTLP_ATTR_STRING);
	assert(strcmp(attrs[0].v.string_val, "a1") == 0);
	otlp_metric_free(m);
	return 0;
}

static int
test_metric_attribute_int(void)
{
	otlp_metric_t *m = otlp_metric_create(
		OTLP_METRIC_GAUGE, "depth", "m", NULL, NULL, 0);
	const struct otlp_attribute *attrs;
	size_t n = 0;

	assert(m != NULL);
	assert(otlp_metric_set_attribute_int(m, "shard", 7) == OTLP_OK);
	attrs = otlp_metric_get_attrs(m, &n);
	assert(attrs != NULL && n == 1);
	assert(attrs[0].type == OTLP_ATTR_INT64);
	assert(attrs[0].v.int64_val == 7);
	otlp_metric_free(m);
	return 0;
}

static int
test_metric_record_counter(void)
{
	otlp_metric_t *m = otlp_metric_create(
		OTLP_METRIC_COUNTER, "hits", NULL, NULL, NULL, 0);

	assert(m != NULL);
	assert(otlp_metric_record(m, 1.0) == OTLP_OK);
	assert(otlp_metric_record(m, 2.0) == OTLP_OK);
	assert(otlp_metric_get_value(m) == 3.0);
	otlp_metric_free(m);
	return 0;
}

static int
test_metric_clone_attrs(void)
{
	otlp_metric_t *m = otlp_metric_create(
		OTLP_METRIC_COUNTER, "requests", NULL, NULL, NULL, 0);
	otlp_metric_t *c;
	const struct otlp_attribute *attrs;
	size_t n = 0;

	assert(m != NULL);
	assert(otlp_metric_set_attribute_string(m, "region", "eu") == OTLP_OK);
	assert(otlp_metric_set_attribute_int(m, "shard", 3) == OTLP_OK);
	c = otlp_metric_clone(m);
	assert(c != NULL);
	attrs = otlp_metric_get_attrs(c, &n);
	assert(attrs != NULL && n == 2);
	assert(strcmp(attrs[0].key, "region") == 0);
	assert(strcmp(attrs[0].v.string_val, "eu") == 0);
	assert(attrs[1].v.int64_val == 3);
	assert(otlp_metric_get_value(c) == otlp_metric_get_value(m));
	otlp_metric_free(c);
	otlp_metric_free(m);
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_metric_struct_size();
	failures += test_metric_create_free();
	failures += test_metric_no_attrs_lazy();
	failures += test_metric_attribute_string();
	failures += test_metric_attribute_int();
	failures += test_metric_record_counter();
	failures += test_metric_clone_attrs();

	if (failures)
		printf("[unit-metric] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-metric] PASS (7 tests)\n");

	return failures ? 1 : 0;
}
