/* SPDX-License-Identifier: Apache-2.0 */
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

#include <otlp-c/span.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Resource field indices (matches src/otlp_schema.h).
 * Duplicated as #defines here so the test is self-contained. */
#define ETSR_F_RESOURCE_SPANS 1
#define RS_F_RESOURCE	       1
#define R_F_ATTRIBUTES	       1
#define KV_F_KEY	       1
#define KV_F_VALUE	       2
#define AV_F_STRING	       1

/* Scan the Resource body at [pos, end) for a KeyValue with the given
 * string key + value. Each KeyValue appears as a length-delimited
 * field 1 (R_F_ATTRIBUTES) in the Resource. Returns 1 on match. */
static int
find_string_attr(const uint8_t *data, size_t pos, size_t end,
		 const char *want_key, const char *want_val)
{
	while (pos < end) {
		size_t kv_pos;
		size_t kv_end;
		int    wt = 0;
		size_t vp = 0;
		size_t vl = 0;
		size_t scan_pos;
		size_t scan_end;

		if (!walker_find_at_level(data, pos, end, R_F_ATTRIBUTES,
					  &wt, &vp, &vl))
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
		if (!walker_find_at_level(data, scan_pos, scan_end,
					  KV_F_KEY, &wt, &vp, &vl))
			continue;
		if (wt != OTLP_PB_WIRE_LEN)
			continue;
		if (vl != strlen(want_key) ||
		    memcmp(data + vp, want_key, vl) != 0)
			continue;
		scan_pos = vp + vl;

		/* Value (KV_F_VALUE = 2) → AnyValue → string (AV_F_STRING = 1). */
		if (!walker_find_at_level(data, scan_pos, scan_end,
					  KV_F_VALUE, &wt, &vp, &vl))
			continue;
		if (wt != OTLP_PB_WIRE_LEN)
			continue;
		{
			size_t av_pos = vp;
			size_t av_end = vp + vl;

			if (!walker_find_at_level(data, av_pos, av_end,
						  AV_F_STRING, &wt, &vp, &vl))
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
descend_to_resource_attrs(const uint8_t *data, size_t len,
			  size_t *out_pos, size_t *out_end)
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

static int
prop_resource_empty(uint64_t seed)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t	    st;
	int		    ok = 0;

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
	otlp_status_t	    st;
	size_t		    kvs_pos = 0;
	size_t		    kvs_end = 0;
	int		    ok = 0;

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
	ok = find_string_attr(buf.data, kvs_pos, kvs_end,
			      "service.name", "svc-x");
out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_resource_extra_attrs_encoded(uint64_t seed)
{
	struct otlp_pb_buf	     buf = { 0 };
	otlp_status_t		     st;
	otlp_resource_attr_t	     attrs[3];
	size_t			     kvs_pos = 0;
	size_t			     kvs_end = 0;
	int			     ok = 0;

	(void) seed;
	attrs[0].key = "service.version";
	attrs[0].value = "1.2.3";
	attrs[1].key = "deployment.environment";
	attrs[1].value = "production";
	attrs[2].key = "host.name";
	attrs[2].value = "web-01";

	st = otlp_pb_buf_init(&buf, 0);
	if (st != OTLP_OK)
		return 0;
	st = otlp_encode_export_trace_service_request(
		&buf, "billing", attrs, 3, NULL, NULL, NULL, 0);
	if (st != OTLP_OK)
		goto out;
	if (!descend_to_resource_attrs(buf.data, buf.len, &kvs_pos, &kvs_end))
		goto out;

	ok = find_string_attr(buf.data, kvs_pos, kvs_end,
			      "service.name", "billing") &&
	     find_string_attr(buf.data, kvs_pos, kvs_end,
			      "service.version", "1.2.3") &&
	     find_string_attr(buf.data, kvs_pos, kvs_end,
			      "deployment.environment", "production") &&
	     find_string_attr(buf.data, kvs_pos, kvs_end,
			      "host.name", "web-01");
out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_resource_attrs_skip_empty(uint64_t seed)
{
	struct otlp_pb_buf	     buf = { 0 };
	otlp_status_t		     st;
	otlp_resource_attr_t	     attrs[2];
	size_t			     kvs_pos = 0;
	size_t			     kvs_end = 0;
	int			     ok = 0;

	(void) seed;
	/* Empty-key or empty-value entries should be omitted. */
	attrs[0].key = "";
	attrs[0].value = "has-empty-key";
	attrs[1].key = "has-empty-value";
	attrs[1].value = "";

	st = otlp_pb_buf_init(&buf, 0);
	if (st != OTLP_OK)
		return 0;
	st = otlp_encode_export_trace_service_request(
		&buf, "svc", attrs, 2, NULL, NULL, NULL, 0);
	if (st != OTLP_OK)
		goto out;
	if (!descend_to_resource_attrs(buf.data, buf.len, &kvs_pos, &kvs_end))
		goto out;

	/* Neither empty entry should be found. service.name should be. */
	ok = find_string_attr(buf.data, kvs_pos, kvs_end,
			      "service.name", "svc") &&
	     !find_string_attr(buf.data, kvs_pos, kvs_end,
			       "", "has-empty-key") &&
	     !find_string_attr(buf.data, kvs_pos, kvs_end,
			       "has-empty-value", "");
out:
	otlp_pb_buf_free(&buf);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_resource_empty,
				 "prop_resource_empty", 5, 1);
	failures += property_run(prop_resource_service_name_only,
				 "prop_resource_service_name_only", 5, 1);
	failures += property_run(prop_resource_extra_attrs_encoded,
				 "prop_resource_extra_attrs_encoded", 5, 1);
	failures += property_run(prop_resource_attrs_skip_empty,
				 "prop_resource_attrs_skip_empty", 5, 1);

	if (failures)
		printf("[property] %d resource-attr property(ies) failed\n",
		       failures);
	else
		printf("[property] all resource-attr properties passed\n");
	return failures ? 1 : 0;
}
