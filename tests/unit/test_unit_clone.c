// SPDX-License-Identifier: Apache-2.0
//
// Deep-clone round-trips (v0.6.2). The coverage re-measurement
// found the clone/copy arms for everything richer than plain
// scalar attributes had never executed: histogram bounds +
// bucket_counts, exp-histogram buckets, status_message /
// trace_state, bytes/array/kvlist attribute copies, and the
// event/link attribute setters entirely. Each case here builds
// an object exercising those arms, clones it, and proves the
// clone independently via byte-equal encoding — then frees both
// (the ASAN/leak gates own the copy-free symmetry).

#include "../test_util.h"
#include "log_internal.h"
#include "metric_internal.h"
#include "otlp_messages.h"
#include "protobuf_encode.h"
#include "span_internal.h"

#include <otlp-c/log.h>
#include <otlp-c/metric.h>
#include <otlp-c/span.h>
#include <otlp-c/status.h>

#include <stdint.h>
#include <string.h>

static void
fill_composites(otlp_span_t *s)
{
	static const uint8_t raw[3] = { 0x01, 0x02, 0xfe };
	static const otlp_value_t items[2] = {
		{ .type = OTLP_VALUE_INT64, .v = { .int64_val = 3 } },
		{ .type = OTLP_VALUE_STRING, .v = { .string_val = "x" } },
	};
	static const otlp_kv_t kvs[1] = {
		{ .key = "inner",
			.value = { .type = OTLP_VALUE_DOUBLE,
				.v = { .double_val = 2.5 } } },
	};

	check_ok(otlp_span_set_attribute_bytes(s, "b", raw, 3));
	check_ok(otlp_span_set_attribute_array(s, "a", items, 2));
	check_ok(otlp_span_set_attribute_kvlist(s, "m", kvs, 1));
}

static int
test_span_deep_clone(void)
{
	otlp_span_t *s;
	otlp_span_t *c;
	struct otlp_pb_buf b1 = { 0 }, b2 = { 0 };
	uint8_t ltid[16], lsid[8];
	size_t i;

	s = otlp_span_create("deep");
	check_true(s != NULL);
	for (i = 0; i < 16; i++)
		ltid[i] = (uint8_t)(0x30 + i);
	for (i = 0; i < 8; i++)
		lsid[i] = (uint8_t)(0x40 + i);

	check_ok(otlp_span_set_status(s, OTLP_STATUS_CODE_ERROR, "boom"));
	check_ok(otlp_span_set_trace_state(s, "v=1"));
	fill_composites(s);

	/* event carrying every attribute type */
	check_ok(otlp_span_add_event(s, "evt", 42));
	check_ok(otlp_span_set_event_attribute_string(s, "s", "v"));
	check_ok(otlp_span_set_event_attribute_int(s, "i", -7));
	check_ok(otlp_span_set_event_attribute_double(s, "d", 1.5));
	check_ok(otlp_span_set_event_attribute_bool(s, "b", true));
	{
		static const uint8_t raw[2] = { 0x00, 0xff };

		check_ok(otlp_span_set_event_attribute_bytes(s, "y", raw, 2));
	}
	{
		static const otlp_value_t items[1] = {
			{ .type = OTLP_VALUE_DOUBLE,
				.v = { .double_val = 0.5 } }
		};
		static const otlp_kv_t kvs[1] = {
			{ .key = "ik",
				.value = { .type = OTLP_VALUE_INT64,
					.v = { .int64_val = 2 } } },
		};

		check_ok(
			otlp_span_set_event_attribute_array(s, "ea", items, 1));
		check_ok(otlp_span_set_event_attribute_kvlist(s, "em", kvs, 1));
	}

	/* link carrying every attribute type */
	check_ok(otlp_span_add_link(s, ltid, lsid));
	check_ok(otlp_span_set_link_attribute_string(s, "ls", "v"));
	check_ok(otlp_span_set_link_attribute_int(s, "li", 9));
	check_ok(otlp_span_set_link_attribute_double(s, "ld", 0.25));
	check_ok(otlp_span_set_link_attribute_bool(s, "lb", true));
	{
		static const uint8_t lraw[2] = { 0x01, 0x02 };

		check_ok(otlp_span_set_link_attribute_bytes(s, "ly", lraw, 2));
	}
	{
		static const otlp_value_t items[1] = {
			{ .type = OTLP_VALUE_STRING,
				.v = { .string_val = "z" } }
		};
		static const otlp_kv_t kvs[1] = {
			{ .key = "lk",
				.value = { .type = OTLP_VALUE_BOOL,
					.v = { .bool_val = true } } },
		};

		check_ok(otlp_span_set_link_attribute_array(s, "la", items, 1));
		check_ok(otlp_span_set_link_attribute_kvlist(s, "lm", kvs, 1));
	}

	/* guards: setters with nothing to attach to */
	check_true(otlp_span_set_event_attribute_string(otlp_span_create("e"),
			   "k",
			   "v") == OTLP_ERR_INVALID_ARGUMENT);

	c = otlp_span_clone(s);
	check_true(c != NULL);

	check_ok(otlp_encode_span_body(&b1, s));
	check_ok(otlp_encode_span_body(&b2, c));
	check_true(b1.len == b2.len && memcmp(b1.data, b2.data, b1.len) == 0);

	otlp_pb_buf_free(&b1);
	otlp_pb_buf_free(&b2);
	otlp_span_free(s);
	otlp_span_free(c);
	return 0;
}

static int
test_metric_deep_clone(void)
{
	static const double bounds[2] = { 1.5, 9.5 };
	static const uint64_t pos[1] = { 4 };
	const otlp_metric_t *arr[1];
	otlp_metric_t *h, *eh, *ch, *ceh;
	struct otlp_pb_buf b1 = { 0 }, b2 = { 0 };

	/* histogram: clone must copy bounds + bucket_counts */
	h = otlp_metric_create(OTLP_METRIC_HISTOGRAM, "h", "1", "d", bounds, 2);
	check_true(h != NULL);
	check_ok(otlp_metric_record(h, 1.0));
	check_ok(otlp_metric_record(h, 5.0));
	check_ok(otlp_metric_record(h, 99.0));
	ch = otlp_metric_clone(h);
	check_true(ch != NULL);

	/* exp-histogram: clone must copy the bucket arrays */
	eh = otlp_metric_create(
		OTLP_METRIC_EXP_HISTOGRAM, "e", "1", "d", NULL, 0);
	check_true(eh != NULL);
	check_ok(otlp_metric_record(eh, 3.0));
	check_ok(otlp_metric_set_exp_histogram(eh, -1, 2, pos, 1, -3, pos, 1));
	{
		static const uint8_t raw[1] = { 0xf0 };
		static const otlp_value_t items[1] = {
			{ .type = OTLP_VALUE_BOOL, .v = { .bool_val = true } }
		};
		static const otlp_kv_t kvs[1] = {
			{ .key = "k",
				.value = { .type = OTLP_VALUE_INT64,
					.v = { .int64_val = 1 } } },
		};

		check_ok(otlp_metric_set_attribute_bytes(eh, "b", raw, 1));
		check_ok(otlp_metric_set_attribute_array(eh, "a", items, 1));
		check_ok(otlp_metric_set_attribute_kvlist(eh, "m", kvs, 1));
	}
	ceh = otlp_metric_clone(eh);
	check_true(ceh != NULL);

	arr[0] = h;
	check_ok(otlp_encode_export_metrics_service_request(
		&b1, "svc", NULL, 0, NULL, NULL, arr, 1));
	arr[0] = ch;
	check_ok(otlp_encode_export_metrics_service_request(
		&b2, "svc", NULL, 0, NULL, NULL, arr, 1));
	check_true(b1.len == b2.len && memcmp(b1.data, b2.data, b1.len) == 0);
	otlp_pb_buf_free(&b1);
	otlp_pb_buf_free(&b2);

	check_ok(otlp_pb_buf_init(&b1, 128));
	check_ok(otlp_pb_buf_init(&b2, 128));
	arr[0] = eh;
	check_ok(otlp_encode_export_metrics_service_request(
		&b1, "svc", NULL, 0, NULL, NULL, arr, 1));
	arr[0] = ceh;
	check_ok(otlp_encode_export_metrics_service_request(
		&b2, "svc", NULL, 0, NULL, NULL, arr, 1));
	check_true(b1.len == b2.len && memcmp(b1.data, b2.data, b1.len) == 0);
	otlp_pb_buf_free(&b1);
	otlp_pb_buf_free(&b2);

	otlp_metric_free(h);
	otlp_metric_free(ch);
	otlp_metric_free(eh);
	otlp_metric_free(ceh);
	return 0;
}

static int
test_log_deep_clone(void)
{
	static const uint8_t raw[2] = { 0xaa, 0xbb };
	static const otlp_value_t items[1] = { { .type = OTLP_VALUE_STRING,
		.v = { .string_val = "s" } } };
	static const otlp_kv_t kvs[1] = {
		{ .key = "k",
			.value = { .type = OTLP_VALUE_BOOL,
				.v = { .bool_val = false } } },
	};
	const otlp_log_record_t *arr[1];
	otlp_log_record_t *lr, *cl;
	struct otlp_pb_buf b1 = { 0 }, b2 = { 0 };
	uint8_t tid[16], sid[8];
	size_t i;

	for (i = 0; i < 16; i++)
		tid[i] = (uint8_t)(0x50 + i);
	for (i = 0; i < 8; i++)
		sid[i] = (uint8_t)(0x60 + i);

	lr = otlp_log_record_create(OTLP_SEVERITY_WARN, "deep body");
	check_true(lr != NULL);
	check_ok(otlp_log_record_set_severity_text(lr, "WARN"));
	check_ok(otlp_log_record_set_trace_id(lr, tid));
	check_ok(otlp_log_record_set_span_id(lr, sid));
	check_ok(otlp_log_record_set_attribute_bytes(lr, "b", raw, 2));
	check_ok(otlp_log_record_set_attribute_array(lr, "a", items, 1));
	check_ok(otlp_log_record_set_attribute_kvlist(lr, "m", kvs, 1));

	cl = otlp_log_record_clone(lr);
	check_true(cl != NULL);

	arr[0] = lr;
	check_ok(otlp_encode_export_logs_service_request(
		&b1, "svc", NULL, 0, NULL, NULL, arr, 1));
	arr[0] = cl;
	check_ok(otlp_encode_export_logs_service_request(
		&b2, "svc", NULL, 0, NULL, NULL, arr, 1));
	check_true(b1.len == b2.len && memcmp(b1.data, b2.data, b1.len) == 0);

	otlp_pb_buf_free(&b1);
	otlp_pb_buf_free(&b2);
	otlp_log_record_free(lr);
	otlp_log_record_free(cl);
	return 0;
}

/* The has_start/has_time emission matrix for NDP and HDP paths
 * (neither / start-only / time-only / both). */
static int
test_metric_time_states(void)
{
	struct otlp_pb_buf b = { 0 };
	const otlp_metric_t *arr[1];
	otlp_metric_t *m;

	m = otlp_metric_create(OTLP_METRIC_GAUGE, "g", "1", "d", NULL, 0);
	check_true(m != NULL);
	check_ok(otlp_metric_record(m, 1.0));
	arr[0] = m;
	/* neither */
	check_ok(otlp_pb_buf_init(&b, 64));
	check_ok(otlp_encode_export_metrics_service_request(
		&b, "s", NULL, 0, NULL, NULL, arr, 1));
	check_true(b.len > 0);
	otlp_pb_buf_free(&b);
	/* start only */
	check_ok(otlp_metric_set_start_time(m, 111));
	check_ok(otlp_pb_buf_init(&b, 64));
	check_ok(otlp_encode_export_metrics_service_request(
		&b, "s", NULL, 0, NULL, NULL, arr, 1));
	otlp_pb_buf_free(&b);
	/* time only */
	m->has_start = false;
	check_ok(otlp_metric_set_time(m, 222));
	check_ok(otlp_pb_buf_init(&b, 64));
	check_ok(otlp_encode_export_metrics_service_request(
		&b, "s", NULL, 0, NULL, NULL, arr, 1));
	otlp_pb_buf_free(&b);
	/* both */
	check_ok(otlp_metric_set_start_time(m, 333));
	check_ok(otlp_pb_buf_init(&b, 64));
	check_ok(otlp_encode_export_metrics_service_request(
		&b, "s", NULL, 0, NULL, NULL, arr, 1));
	otlp_pb_buf_free(&b);
	otlp_metric_free(m);

	/* HDP path through the same matrix */
	m = otlp_metric_create(OTLP_METRIC_HISTOGRAM, "h", "1", "d", NULL, 0);
	check_true(m != NULL);
	check_ok(otlp_metric_record(m, 2.0));
	arr[0] = m;
	check_ok(otlp_pb_buf_init(&b, 64));
	check_ok(otlp_encode_export_metrics_service_request(
		&b, "s", NULL, 0, NULL, NULL, arr, 1));
	otlp_pb_buf_free(&b);
	check_ok(otlp_metric_set_start_time(m, 111));
	check_ok(otlp_pb_buf_init(&b, 64));
	check_ok(otlp_encode_export_metrics_service_request(
		&b, "s", NULL, 0, NULL, NULL, arr, 1));
	otlp_pb_buf_free(&b);
	m->has_start = false;
	check_ok(otlp_metric_set_time(m, 222));
	check_ok(otlp_pb_buf_init(&b, 64));
	check_ok(otlp_encode_export_metrics_service_request(
		&b, "s", NULL, 0, NULL, NULL, arr, 1));
	otlp_pb_buf_free(&b);
	check_ok(otlp_metric_set_start_time(m, 333));
	check_ok(otlp_pb_buf_init(&b, 64));
	check_ok(otlp_encode_export_metrics_service_request(
		&b, "s", NULL, 0, NULL, NULL, arr, 1));
	otlp_pb_buf_free(&b);
	otlp_metric_free(m);

	otlp_span_free(NULL);
	otlp_metric_free(NULL);
	otlp_log_record_free(NULL);
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_span_deep_clone();
	failures += test_metric_deep_clone();
	failures += test_log_deep_clone();
	failures += test_metric_time_states();

	if (failures)
		printf("[unit-clone] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-clone] PASS (4 tests)\n");
	return failures ? 1 : 0;
}
