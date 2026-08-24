/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Property tests for Resource attributes — the OTLP Resource message
 * carries arbitrary KeyValue attributes (service.version,
 * deployment.environment, host.name, etc.) alongside service.name.
 *
 *   prop_resource_empty                   — no service, no attrs → 0 bytes.
 *   prop_resource_service_name_only       — service.name emitted, no extras.
 *   prop_resource_extra_attrs_encoded     — 3 extra attrs all present on wire.
 *   prop_resource_attrs_skip_empty_values — empty-string attr is omitted.
 *
 * Uses the shared walker (walker.h) to descend the wire tree one level
 * at a time, then scans the Resource's attribute list for each key.
 */
#include "decoder.h"
#include "prng.h"
#include "property_harness.h"
#include "walker.h"

#include "../src/otlp_messages.h"
#include "../src/protobuf_encode.h"

#include <otlp-c/exporter.h>
#include <otlp-c/exporter.h>
#include <otlp-c/span.h>

#include "../src/exporter_internal.h"

#include "../src/exporter_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Resource field indices (matches src/otlp_schema.h).
 * Duplicated as #defines here so the test is self-contained. */
#define ETSR_F_RESOURCE_SPANS 1
#define RS_F_RESOURCE 1
#define R_F_ATTRIBUTES 1
#define KV_F_KEY 1
#define KV_F_VALUE 2
#define AV_F_STRING 1

/* Scan the Resource body at [pos, end) for a KeyValue with the given
 * string key + value. Each KeyValue appears as a length-delimited
 * field 1 (R_F_ATTRIBUTES) in the Resource. Returns 1 on match. */
static int
find_string_attr(const uint8_t *data,
	size_t pos,
	size_t end,
	const char *want_key,
	const char *want_val)
{
	while (pos < end)
	{
		size_t kv_pos;
		size_t kv_end;
		int wt = 0;
		size_t vp = 0;
		size_t vl = 0;
		size_t scan_pos;
		size_t scan_end;

		if (!walker_find_at_level(
			    data, pos, end, R_F_ATTRIBUTES, &wt, &vp, &vl))
			return 0;
		if (wt != OTLP_PB_WIRE_LEN)
			return 0;
		/* vp..vp+vl is this KeyValue's body. Parse it. */
		kv_pos = vp;
		kv_end = vp + vl;
		/* Advance outer pos past this field for the next iteration. */
		pos = vp + vl;

		/* Key (KV_F_KEY = 1). */
		scan_pos = kv_pos;
		scan_end = kv_end;
		if (!walker_find_at_level(
			    data, scan_pos, scan_end, KV_F_KEY, &wt, &vp, &vl))
			continue;
		if (wt != OTLP_PB_WIRE_LEN)
			continue;
		if (vl != strlen(want_key) ||
			memcmp(data + vp, want_key, vl) != 0)
			continue;
		scan_pos = vp + vl;

		/* Value (KV_F_VALUE = 2) → AnyValue → string (AV_F_STRING = 1).
		 */
		if (!walker_find_at_level(data,
			    scan_pos,
			    scan_end,
			    KV_F_VALUE,
			    &wt,
			    &vp,
			    &vl))
			continue;
		if (wt != OTLP_PB_WIRE_LEN)
			continue;
		{
			size_t av_pos = vp;
			size_t av_end = vp + vl;

			if (!walker_find_at_level(data,
				    av_pos,
				    av_end,
				    AV_F_STRING,
				    &wt,
				    &vp,
				    &vl))
				continue;
			if (wt != OTLP_PB_WIRE_LEN)
				continue;
			if (vl != strlen(want_val) ||
				memcmp(data + vp, want_val, vl) != 0)
				continue;
		}
		return 1;
	}
	return 0;
}

/* Descend: ExportTraceServiceRequest → ResourceSpans → Resource body.
 * The Resource body is where repeated KeyValue fields live. */
static int
descend_to_resource_attrs(const uint8_t *data,
	size_t len,
	size_t *out_pos,
	size_t *out_end)
{
	size_t pos = 0;
	size_t end = len;

	if (!walker_descend(data, &pos, &end, ETSR_F_RESOURCE_SPANS))
		return 0;
	if (!walker_descend(data, &pos, &end, RS_F_RESOURCE))
		return 0;
	*out_pos = pos;
	*out_end = end;
	return 1;
}

/* Encode traces with a public attr array by building the owned
 * internal attributes through the exporter (the same path the
 * library itself uses). */
static otlp_status_t
encode_traces_with_attrs(struct otlp_pb_buf *buf,
	const char *service,
	const otlp_resource_attr_t *attrs,
	size_t n)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	const struct otlp_attribute *internal;
	size_t n_internal = 0;
	otlp_status_t st;

	memset(&opts, 0, sizeof(opts));
	opts.service_name = service;
	opts.resource_attributes = attrs;
	opts.n_resource_attributes = n;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return OTLP_ERR_NOMEM;
	internal = otlp_exporter_get_resource_attrs(exp, &n_internal);
	st = otlp_encode_export_trace_service_request(
		buf, service, internal, n_internal, NULL, NULL, NULL, 0);
	otlp_exporter_free(exp);
	return st;
}

static int
prop_resource_empty(uint64_t seed)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t st;
	int ok = 0;

	(void) seed;
	st = otlp_pb_buf_init(&buf, 0);
	if (st != OTLP_OK)
		return 0;
	st = otlp_encode_export_trace_service_request(
		&buf, NULL, NULL, 0, NULL, NULL, NULL, 0);
	if (st == OTLP_OK)
		ok = (buf.len == 0);
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_resource_service_name_only(uint64_t seed)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t st;
	size_t kvs_pos = 0;
	size_t kvs_end = 0;
	int ok = 0;

	(void) seed;
	st = otlp_pb_buf_init(&buf, 0);
	if (st != OTLP_OK)
		return 0;
	st = otlp_encode_export_trace_service_request(
		&buf, "svc-x", NULL, 0, NULL, NULL, NULL, 0);
	if (st != OTLP_OK)
		goto out;
	if (!descend_to_resource_attrs(buf.data, buf.len, &kvs_pos, &kvs_end))
		goto out;
	ok = find_string_attr(
		buf.data, kvs_pos, kvs_end, "service.name", "svc-x");
out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_resource_extra_attrs_encoded(uint64_t seed)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t st;
	otlp_resource_attr_t attrs[3];
	size_t kvs_pos = 0;
	size_t kvs_end = 0;
	int ok = 0;

	(void) seed;
	memset(attrs, 0, sizeof(attrs));
	attrs[0].key = "service.version";
	attrs[0].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "1.2.3" } };
	attrs[1].key = "deployment.environment";
	attrs[1].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "production" } };
	attrs[2].key = "host.name";
	attrs[2].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "web-01" } };

	st = otlp_pb_buf_init(&buf, 0);
	if (st != OTLP_OK)
		return 0;
	st = encode_traces_with_attrs(&buf, "billing", attrs, 3);
	if (st != OTLP_OK)
		goto out;
	if (!descend_to_resource_attrs(buf.data, buf.len, &kvs_pos, &kvs_end))
		goto out;

	ok = find_string_attr(
		     buf.data, kvs_pos, kvs_end, "service.name", "billing") &&
		find_string_attr(buf.data,
			kvs_pos,
			kvs_end,
			"service.version",
			"1.2.3") &&
		find_string_attr(buf.data,
			kvs_pos,
			kvs_end,
			"deployment.environment",
			"production") &&
		find_string_attr(
			buf.data, kvs_pos, kvs_end, "host.name", "web-01");
out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_resource_attrs_skip_empty(uint64_t seed)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t st;
	otlp_resource_attr_t attrs[2];
	size_t kvs_pos = 0;
	size_t kvs_end = 0;
	int ok = 0;

	(void) seed;
	/* Empty-key or empty-value entries should be omitted. */
	memset(attrs, 0, sizeof(attrs));
	attrs[0].key = "";
	attrs[0].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "has-empty-key" } };
	attrs[1].key = "has-empty-value";
	attrs[1].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "" } };

	st = otlp_pb_buf_init(&buf, 0);
	if (st != OTLP_OK)
		return 0;
	st = encode_traces_with_attrs(&buf, "svc", attrs, 2);
	if (st != OTLP_OK)
		goto out;
	if (!descend_to_resource_attrs(buf.data, buf.len, &kvs_pos, &kvs_end))
		goto out;

	/* Neither empty entry should be found. service.name should be. */
	ok = find_string_attr(
		     buf.data, kvs_pos, kvs_end, "service.name", "svc") &&
		!find_string_attr(
			buf.data, kvs_pos, kvs_end, "", "has-empty-key") &&
		!find_string_attr(
			buf.data, kvs_pos, kvs_end, "has-empty-value", "");
out:
	otlp_pb_buf_free(&buf);
	return ok;
}

/* ── Typed-value properties (v0.5.24) ────────────────────────── */

/* Check whether a given key string appears at the Resource level
 * (as a KeyValue entry). Used to verify typed attrs are emitted
 * without duplicating the full wire-walk for each type's value
 * encoding — the AnyValue encoder tests cover the value bytes. */
static int
find_key(const uint8_t *data, size_t pos, size_t end, const char *want_key)
{
	size_t want_len = strlen(want_key);

	while (pos < end)
	{
		size_t kv_pos;
		size_t kv_end;
		int wt = 0;
		size_t vp = 0;
		size_t vl = 0;

		if (!walker_find_at_level(
			    data, pos, end, R_F_ATTRIBUTES, &wt, &vp, &vl))
			return 0;
		if (wt != OTLP_PB_WIRE_LEN)
			return 0;
		kv_pos = vp;
		kv_end = vp + vl;
		pos = vp + vl;

		if (walker_find_at_level(
			    data, kv_pos, kv_end, KV_F_KEY, &wt, &vp, &vl))
		{
			if (wt == OTLP_PB_WIRE_LEN && vl == want_len &&
				memcmp(data + vp, want_key, want_len) == 0)
				return 1;
		}
	}
	return 0;
}

static int
prop_resource_typed_int64(uint64_t seed)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t st;
	otlp_resource_attr_t attrs[1];
	size_t kvs_pos = 0;
	size_t kvs_end = 0;
	int ok = 0;

	(void) seed;
	memset(attrs, 0, sizeof(attrs));
	attrs[0].key = "process.pid";
	attrs[0].value = (otlp_value_t){ .type = OTLP_VALUE_INT64,
		.v = { .int64_val = 4242 } };

	st = otlp_pb_buf_init(&buf, 0);
	if (st != OTLP_OK)
		return 0;
	st = encode_traces_with_attrs(&buf, "svc", attrs, 1);
	if (st != OTLP_OK)
		goto out;
	if (!descend_to_resource_attrs(buf.data, buf.len, &kvs_pos, &kvs_end))
		goto out;
	/* Key is present + service.name is present (backward compat). */
	ok = find_key(buf.data, kvs_pos, kvs_end, "process.pid") &&
		find_string_attr(
			buf.data, kvs_pos, kvs_end, "service.name", "svc");
out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_resource_typed_bool(uint64_t seed)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t st;
	otlp_resource_attr_t attrs[1];
	size_t kvs_pos = 0;
	size_t kvs_end = 0;
	int ok = 0;

	(void) seed;
	memset(attrs, 0, sizeof(attrs));
	attrs[0].key = "cloud.auto_scale";
	attrs[0].value = (otlp_value_t){ .type = OTLP_VALUE_BOOL,
		.v = { .bool_val = true } };

	st = otlp_pb_buf_init(&buf, 0);
	if (st != OTLP_OK)
		return 0;
	st = encode_traces_with_attrs(&buf, "svc", attrs, 1);
	if (st != OTLP_OK)
		goto out;
	if (!descend_to_resource_attrs(buf.data, buf.len, &kvs_pos, &kvs_end))
		goto out;
	ok = find_key(buf.data, kvs_pos, kvs_end, "cloud.auto_scale");
out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_resource_mixed_types(uint64_t seed)
{
	/* Verify string + int64 + bool + double all coexist on the wire. */
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t st;
	otlp_resource_attr_t attrs[4];
	size_t kvs_pos = 0;
	size_t kvs_end = 0;
	int ok = 0;

	(void) seed;
	memset(attrs, 0, sizeof(attrs));
	attrs[0].key = "service.version";
	attrs[0].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "1.0.0" } };
	attrs[1].key = "process.pid";
	attrs[1].value = (otlp_value_t){ .type = OTLP_VALUE_INT64,
		.v = { .int64_val = 999 } };
	attrs[2].key = "system.crashed";
	attrs[2].value = (otlp_value_t){ .type = OTLP_VALUE_BOOL,
		.v = { .bool_val = false } };
	attrs[3].key = "cpu.load";
	attrs[3].value = (otlp_value_t){ .type = OTLP_VALUE_DOUBLE,
		.v = { .double_val = 3.14 } };

	st = otlp_pb_buf_init(&buf, 0);
	if (st != OTLP_OK)
		return 0;
	st = encode_traces_with_attrs(&buf, "svc", attrs, 4);
	if (st != OTLP_OK)
		goto out;
	if (!descend_to_resource_attrs(buf.data, buf.len, &kvs_pos, &kvs_end))
		goto out;

	/* All four keys present. String value still round-trips
	 * (backward compat). */
	ok = find_string_attr(
		     buf.data, kvs_pos, kvs_end, "service.name", "svc") &&
		find_string_attr(buf.data,
			kvs_pos,
			kvs_end,
			"service.version",
			"1.0.0") &&
		find_key(buf.data, kvs_pos, kvs_end, "process.pid") &&
		find_key(buf.data, kvs_pos, kvs_end, "system.crashed") &&
		find_key(buf.data, kvs_pos, kvs_end, "cpu.load");

out:
	otlp_pb_buf_free(&buf);
	return ok;
}


/* Resource attributes are a map: duplicate keys in opts collapse
 * last-write-wins at exporter-create, so duplicate KeyValues can
 * never reach the wire (v0.5.78). */
static int
prop_resource_attrs_dedup(uint64_t seed)
{
	otlp_resource_attr_t attrs[3];
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	const struct otlp_attribute *stored;
	size_t n = 99;
	int ok = 0;

	(void) seed;
	memset(attrs, 0, sizeof(attrs));
	attrs[0].key = "host.name";
	attrs[0].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "web-01" } };
	attrs[1].key = "host.name";
	attrs[1].value = (otlp_value_t){ .type = OTLP_VALUE_INT64,
		.v = { .int64_val = 77 } };
	attrs[2].key = "zone";
	attrs[2].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "a" } };

	memset(&opts, 0, sizeof(opts));
	opts.service_name = "svc";
	opts.resource_attributes = attrs;
	opts.n_resource_attributes = 3;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	stored = otlp_exporter_get_resource_attrs(exp, &n);
	ok = (n == 2) && stored && strcmp(stored[0].key, "host.name") == 0 &&
		stored[0].type == OTLP_ATTR_INT64 &&
		stored[0].v.int64_val == 77 &&
		strcmp(stored[1].key, "zone") == 0;
	otlp_exporter_free(exp);
	return ok;
}

/* A "service.name" resource attr is dropped when the dedicated
 * service_name opt is set — otherwise it would duplicate the
 * auto-emitted service.name KeyValue. */
static int
prop_resource_service_name_wins(uint64_t seed)
{
	otlp_resource_attr_t attrs[2];
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	const struct otlp_attribute *stored;
	size_t n = 99;
	size_t i;
	int saw_svc = 0;
	int ok = 0;

	(void) seed;
	memset(attrs, 0, sizeof(attrs));
	attrs[0].key = "service.name";
	attrs[0].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "from-attrs" } };
	attrs[1].key = "zone";
	attrs[1].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "a" } };

	memset(&opts, 0, sizeof(opts));
	opts.service_name = "from-opt";
	opts.resource_attributes = attrs;
	opts.n_resource_attributes = 2;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	stored = otlp_exporter_get_resource_attrs(exp, &n);
	for (i = 0; stored && i < n; i++)
		if (strcmp(stored[i].key, "service.name") == 0)
			saw_svc = 1;
	ok = (n == 1) && !saw_svc && strcmp(stored[0].key, "zone") == 0;
	otlp_exporter_free(exp);
	return ok;
}

/* v0.5.92: resource attributes take the full otlp_value_t model.
 * BYTES (newly supported) round-trips through the exporter. */
static int
prop_resource_full_value_model(uint64_t seed)
{
	const uint8_t payload[3] = { 0xde, 0xad, 0x00 };
	otlp_resource_attr_t attrs[2];
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	const struct otlp_attribute *stored;
	size_t n = 0;
	int ok = 0;

	(void) seed;
	memset(attrs, 0, sizeof(attrs));
	attrs[0].key = "blob";
	attrs[0].value = (otlp_value_t){ .type = OTLP_VALUE_BYTES,
		.v = { .bytes_val = { .data = payload, .len = 3 } } };
	attrs[1].key = "pid";
	attrs[1].value = (otlp_value_t){ .type = OTLP_VALUE_INT64,
		.v = { .int64_val = 42 } };

	memset(&opts, 0, sizeof(opts));
	opts.service_name = "svc";
	opts.resource_attributes = attrs;
	opts.n_resource_attributes = 2;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	stored = otlp_exporter_get_resource_attrs(exp, &n);
	ok = (n == 2) && stored && stored[0].type == OTLP_ATTR_BYTES &&
		stored[0].v.bytes_val.len == 3 &&
		stored[0].v.bytes_val.data[0] == 0xde &&
		stored[1].v.int64_val == 42;
	otlp_exporter_free(exp);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures +=
		property_run(prop_resource_empty, "prop_resource_empty", 5, 1);
	failures += property_run(prop_resource_service_name_only,
		"prop_resource_service_name_only",
		5,
		1);
	failures += property_run(prop_resource_extra_attrs_encoded,
		"prop_resource_extra_attrs_encoded",
		5,
		1);
	failures += property_run(prop_resource_attrs_skip_empty,
		"prop_resource_attrs_skip_empty",
		5,
		1);
	failures += property_run(
		prop_resource_typed_int64, "prop_resource_typed_int64", 5, 1);
	failures += property_run(
		prop_resource_typed_bool, "prop_resource_typed_bool", 5, 1);
	failures += property_run(
		prop_resource_mixed_types, "prop_resource_mixed_types", 5, 1);
	failures += property_run(
		prop_resource_attrs_dedup, "prop_resource_attrs_dedup", 5, 1);
	failures += property_run(prop_resource_service_name_wins,
		"prop_resource_service_name_wins",
		5,
		1);
	failures += property_run(prop_resource_full_value_model,
		"prop_resource_full_value_model",
		5,
		1);

	if (failures)
		printf("[property] %d resource-attr property(ies) failed\n",
			failures);
	else
		printf("[property] all resource-attr properties passed\n");
	return failures ? 1 : 0;
}
