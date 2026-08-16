// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the log record lifecycle. Complements the
// property tests with known-answer assertions for each public
// setter and the lazy attribute-array contract.

#include "../../src/log_internal.h"

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
	/* 80 bytes at v0.5.69. 512B is a generous ceiling that still
	 * catches a return to the inline-array layout. */
	assert(otlp_log_struct_size() <= 512);
	printf("[unit-log] sizeof(otlp_log_record)=%zu bytes\n",
		otlp_log_struct_size());
	return 0;
}

static int
test_log_create_free(void)
{
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_INFO, "hello");

	assert(lr != NULL);
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

	assert(lr != NULL);
	attrs = otlp_log_get_attrs(lr, &n);
	assert(attrs == NULL);
	assert(n == 0);
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

	assert(lr != NULL);
	assert(otlp_log_record_set_attribute_string(lr, "db", "pg") == OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	assert(attrs != NULL && n == 1);
	assert(strcmp(attrs[0].key, "db") == 0);
	assert(attrs[0].type == OTLP_ATTR_STRING);
	assert(strcmp(attrs[0].v.string_val, "pg") == 0);
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

	assert(lr != NULL);
	assert(otlp_log_record_set_attribute_int(lr, "retry", 3) == OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	assert(attrs != NULL && n == 1);
	assert(attrs[0].type == OTLP_ATTR_INT64);
	assert(attrs[0].v.int64_val == 3);
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

	assert(lr != NULL);
	assert(otlp_log_record_set_attribute_double(lr, "dur_s", 0.125) ==
		OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	assert(attrs != NULL && n == 1);
	assert(attrs[0].type == OTLP_ATTR_DOUBLE);
	assert(attrs[0].v.double_val == 0.125);
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

	assert(lr != NULL);
	assert(otlp_log_record_set_attribute_bool(lr, "retrying", true) ==
		OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	assert(attrs != NULL && n == 1);
	assert(attrs[0].type == OTLP_ATTR_BOOL);
	assert(attrs[0].v.bool_val == true);
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

	assert(lr != NULL);
	assert(otlp_log_record_set_attribute_bytes(lr, "frame", payload, 4) ==
		OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	assert(attrs != NULL && n == 1);
	assert(attrs[0].type == OTLP_ATTR_BYTES);
	assert(attrs[0].v.bytes_val.len == 4);
	assert(attrs[0].v.bytes_val.data[3] == 0xff);
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

	assert(lr != NULL);
	assert(otlp_log_record_set_attribute_int(lr, "k", 1) == OTLP_OK);
	assert(otlp_log_record_set_attribute_string(lr, "k", "replaced") ==
		OTLP_OK);
	attrs = otlp_log_get_attrs(lr, &n);
	assert(n == 1);
	assert(attrs[0].type == OTLP_ATTR_STRING);
	assert(strcmp(attrs[0].v.string_val, "replaced") == 0);
	otlp_log_record_free(lr);
	return 0;
}

static int
test_log_severity_text(void)
{
	otlp_log_record_t *lr =
		otlp_log_record_create(OTLP_SEVERITY_INFO, "hello");

	assert(lr != NULL);
	assert(otlp_log_record_set_severity_text(lr, "INFO") == OTLP_OK);
	assert(strcmp(otlp_log_get_severity_text(lr), "INFO") == 0);
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

	assert(lr != NULL);
	assert(otlp_log_record_set_attribute_string(lr, "svc", "api") ==
		OTLP_OK);
	assert(otlp_log_record_set_attribute_int(lr, "attempt", 2) == OTLP_OK);
	assert(otlp_log_record_set_attribute_bytes(lr, "frame", frame, 2) ==
		OTLP_OK);
	c = otlp_log_record_clone(lr);
	assert(c != NULL);
	attrs = otlp_log_get_attrs(c, &n);
	assert(attrs != NULL && n == 3);
	assert(strcmp(attrs[0].key, "svc") == 0);
	assert(strcmp(attrs[0].v.string_val, "api") == 0);
	assert(attrs[1].v.int64_val == 2);
	assert(attrs[2].type == OTLP_ATTR_BYTES);
	assert(attrs[2].v.bytes_val.len == 2);
	assert(attrs[2].v.bytes_val.data[1] == 0x0b);
	assert(attrs[2].v.bytes_val.data != frame); /* deep copy */
	assert(strcmp(otlp_log_get_body(c), "hello") == 0);
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
	failures += test_log_clone_attrs();

	if (failures)
		printf("[unit-log] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-log] PASS (11 tests)\n");

	return failures ? 1 : 0;
}
