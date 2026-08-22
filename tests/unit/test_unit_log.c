// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the log record lifecycle. Complements the
// property tests with known-answer assertions for each public
// setter and the lazy attribute-array contract.

#include "../../src/log_internal.h"
#include "../test_util.h"

#include <otlp-c/log.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Regression guard (v0.5.69): sizeof(struct otlp_log_record) must
 * stay small. Pre-v0.5.69 the inline attrs[128] array made the
 * struct 4168 bytes — 98% of every record allocation, zeroed on
 * every create/clone even though many log records carry no
 * attributes. Logs are the highest-volume signal, so this was the
 * dominant per-record cost. Attrs are now a lazily heap-allocated
 * pointer. */
static int
test_log_struct_size(void)
{
	/* 88 bytes at v0.5.75 (vec adds a capacity field). 256B is a
	 * generous ceiling that still catches a return to the
	 * inline-array layout. */
	check_true(otlp_log_struct_size() <= 256);
	printf("[unit-log] sizeof(otlp_log_record)=%zu bytes\n",
		otlp_log_struct_size());
	return 0;
}

static int
test_log_create_free(void)
{
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_INFO, "hello");

	check_true(lr != NULL);
	otlp_log_record_free(lr);
	return 0;
}

static int
test_log_no_attrs_lazy(void)
{
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_INFO, "hello");
	const struct otlp_attribute *attrs;
	size_t n = 99;

	check_true(lr != NULL);
	attrs = otlp_log_get_attrs(lr, &n);
	check_true(attrs == NULL);
	check_true(n == 0);
	otlp_log_record_free(lr);
	return 0;
}

static int
test_log_attribute_string(void)
{
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_WARN, "slow query");
	const struct otlp_attribute *attrs;
	size_t n = 0;

	check_true(lr != NULL);
	check_true(otlp_log_record_set_attribute_string(lr, "db", "pg") ==
		OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	check_true(attrs != NULL && n == 1);
	check_true(strcmp(attrs[0].key, "db") == 0);
	check_true(attrs[0].type == OTLP_ATTR_STRING);
	check_true(strcmp(attrs[0].v.string_val, "pg") == 0);
	otlp_log_record_free(lr);
	return 0;
}

static int
test_log_attribute_int(void)
{
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_ERROR, "timeout");
	const struct otlp_attribute *attrs;
	size_t n = 0;

	check_true(lr != NULL);
	check_true(
		otlp_log_record_set_attribute_int(lr, "retry", 3) == OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	check_true(attrs != NULL && n == 1);
	check_true(attrs[0].type == OTLP_ATTR_INT64);
	check_true(attrs[0].v.int64_val == 3);
	otlp_log_record_free(lr);
	return 0;
}

static int
test_log_attribute_double(void)
{
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_INFO, "timing");
	const struct otlp_attribute *attrs;
	size_t n = 0;

	check_true(lr != NULL);
	check_true(otlp_log_record_set_attribute_double(lr, "dur_s", 0.125) ==
		OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	check_true(attrs != NULL && n == 1);
	check_true(attrs[0].type == OTLP_ATTR_DOUBLE);
	check_true(attrs[0].v.double_val == 0.125);
	otlp_log_record_free(lr);
	return 0;
}

static int
test_log_attribute_bool(void)
{
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_WARN, "degraded");
	const struct otlp_attribute *attrs;
	size_t n = 0;

	check_true(lr != NULL);
	check_true(otlp_log_record_set_attribute_bool(lr, "retrying", true) ==
		OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	check_true(attrs != NULL && n == 1);
	check_true(attrs[0].type == OTLP_ATTR_BOOL);
	check_true(attrs[0].v.bool_val == true);
	otlp_log_record_free(lr);
	return 0;
}

static int
test_log_attribute_bytes(void)
{
	const uint8_t payload[4] = { 0x01, 0x02, 0x03, 0xff };
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_ERROR, "corrupt frame");
	const struct otlp_attribute *attrs;
	size_t n = 0;

	check_true(lr != NULL);
	check_true(otlp_log_record_set_attribute_bytes(
			   lr, "frame", payload, 4) == OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	check_true(attrs != NULL && n == 1);
	check_true(attrs[0].type == OTLP_ATTR_BYTES);
	check_true(attrs[0].v.bytes_val.len == 4);
	check_true(attrs[0].v.bytes_val.data[3] == 0xff);
	otlp_log_record_free(lr);
	return 0;
}

static int
test_log_attribute_upsert(void)
{
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_INFO, "hello");
	const struct otlp_attribute *attrs;
	size_t n = 0;

	check_true(lr != NULL);
	check_true(otlp_log_record_set_attribute_int(lr, "k", 1) == OTLP_OK);
	check_true(otlp_log_record_set_attribute_string(lr, "k", "replaced") ==
		OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	check_true(n == 1);
	check_true(attrs[0].type == OTLP_ATTR_STRING);
	check_true(strcmp(attrs[0].v.string_val, "replaced") == 0);
	otlp_log_record_free(lr);
	return 0;
}

static int
test_log_attribute_array_kvlist(void)
{
	const otlp_value_t items[2] = {
		{ .type = OTLP_VALUE_DOUBLE, .v = { .double_val = 0.25 } },
		{ .type = OTLP_VALUE_STRING, .v = { .string_val = "p99" } },
	};
	const otlp_kv_t kvs[1] = {
		{ .key = "mode",
			.value = { .type = OTLP_VALUE_STRING,
				.v = { .string_val = "fast" } } },
	};
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_INFO, "stats");
	const struct otlp_attribute *attrs;
	size_t n = 0;

	check_true(lr != NULL);
	check_true(otlp_log_record_set_attribute_array(lr, "pcts", items, 2) ==
		OTLP_OK);
	check_true(otlp_log_record_set_attribute_kvlist(lr, "cfg", kvs, 1) ==
		OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	check_true(n == 2);
	check_true(attrs[0].type == OTLP_ATTR_ARRAY);
	check_true(attrs[0].v.array_val->items[0].v.double_val == 0.25);
	check_true(strcmp(attrs[0].v.array_val->items[1].v.string_val, "p99") ==
		0);
	check_true(attrs[1].type == OTLP_ATTR_KVLIST);
	check_true(strcmp(attrs[1].v.kvlist_val->entries[0].value.v.string_val,
			   "fast") == 0);
	otlp_log_record_free(lr);
	return 0;
}

static int
test_log_severity_text(void)
{
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_INFO, "hello");

	check_true(lr != NULL);
	check_true(otlp_log_record_set_severity_text(lr, "INFO") == OTLP_OK);
	check_true(strcmp(otlp_log_get_severity_text(lr), "INFO") == 0);
	otlp_log_record_free(lr);
	return 0;
}

static int
test_log_clone_attrs(void)
{
	const uint8_t frame[2] = { 0x0a, 0x0b };
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_INFO, "hello");
	otlp_log_record_t *c;
	const struct otlp_attribute *attrs;
	size_t n = 0;

	check_true(lr != NULL);
	check_true(otlp_log_record_set_attribute_string(lr, "svc", "api") ==
		OTLP_OK);
	check_true(
		otlp_log_record_set_attribute_int(lr, "attempt", 2) == OTLP_OK);
	check_true(otlp_log_record_set_attribute_bytes(lr, "frame", frame, 2) ==
		OTLP_OK);
	c = otlp_log_record_clone(lr);
	check_true(c != NULL);
	attrs = otlp_log_get_attrs(c, &n);
	check_true(attrs != NULL && n == 3);
	check_true(strcmp(attrs[0].key, "svc") == 0);
	check_true(strcmp(attrs[0].v.string_val, "api") == 0);
	check_true(attrs[1].v.int64_val == 2);
	check_true(attrs[2].type == OTLP_ATTR_BYTES);
	check_true(attrs[2].v.bytes_val.len == 2);
	check_true(attrs[2].v.bytes_val.data[1] == 0x0b);
	check_true(attrs[2].v.bytes_val.data != frame); /* deep copy */
	check_true(strcmp(otlp_log_get_body(c), "hello") == 0);
	otlp_log_record_free(c);
	otlp_log_record_free(lr);
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_log_struct_size();
	failures += test_log_create_free();
	failures += test_log_no_attrs_lazy();
	failures += test_log_attribute_string();
	failures += test_log_attribute_int();
	failures += test_log_attribute_double();
	failures += test_log_attribute_bool();
	failures += test_log_attribute_bytes();
	failures += test_log_severity_text();
	failures += test_log_attribute_upsert();
	failures += test_log_attribute_array_kvlist();
	failures += test_log_clone_attrs();

	if (failures)
		printf("[unit-log] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-log] PASS (12 tests)\n");

	return failures ? 1 : 0;
}
