// SPDX-License-Identifier: Apache-2.0
//
// UTF-8 boundary validation (v0.5.103). OTLP string fields are
// proto3 `string` — they MUST be valid UTF-8, and Go-based
// collectors (otelcol) reject the whole ExportRequest on unmarshal
// otherwise. The library therefore validates at the API boundary
// and fails the setter (OTLP_ERR_UTF8) instead of letting one bad
// value kill a whole batch at the collector. `bytes` values are
// exempt (proto3 `bytes` accepts anything).

#include "../test_util.h"
#include "internal_util.h"

#include <otlp-c/exporter.h>
#include <otlp-c/log.h>
#include <otlp-c/metric.h>
#include <otlp-c/span.h>
#include <otlp-c/status.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ── The validator itself ─────────────────────────────────────── */

static int
test_validator_valid(void)
{
	check_true(otlp_str_is_utf8(NULL));
	check_true(otlp_str_is_utf8(""));
	check_true(otlp_str_is_utf8("plain ascii"));
	check_true(otlp_str_is_utf8("h\xc3\xa9llo")); /* 2-byte */
	check_true(otlp_str_is_utf8("\xe2\x82\xac")); /* 3-byte € */
	check_true(otlp_str_is_utf8(
		"\xf0\x9f\x98\x80\x21")); /* 4-byte emoji + ! */
	check_true(otlp_str_is_utf8(
		"mixed \xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80 end"));
	check_true(otlp_str_is_utf8("\x7f")); /* DEL is valid UTF-8 */
	return 0;
}

static int
test_validator_invalid(void)
{
	check_true(!otlp_str_is_utf8("\x80")); /* lone continuation */
	check_true(!otlp_str_is_utf8("\xbf\xbf")); /* two continuations */
	check_true(!otlp_str_is_utf8("a\xc3")); /* truncated 2-byte */
	check_true(!otlp_str_is_utf8("\xe2\x82")); /* truncated 3-byte */
	check_true(!otlp_str_is_utf8("\xf0\x9f\x98")); /* truncated 4-byte */
	check_true(!otlp_str_is_utf8("\xc0\x80")); /* overlong NUL */
	check_true(!otlp_str_is_utf8("\xe0\x80\x80")); /* overlong 3-byte */
	check_true(!otlp_str_is_utf8("\xed\xa0\x80")); /* UTF-16 surrogate */
	check_true(!otlp_str_is_utf8("\xed\xbf\xbf")); /* high surrogate */
	check_true(!otlp_str_is_utf8("\xf8\x80\x80\x80")); /* 5-byte lead */
	check_true(!otlp_str_is_utf8("\xfe"));
	check_true(!otlp_str_is_utf8("\xff\xff"));
	check_true(!otlp_str_is_utf8(
		"ok then \xf0\x9f\x98")); /* valid prefix, bad tail */
	return 0;
}

/* ── Per-surface rejection ────────────────────────────────────── */

static const char BAD[] = "bad \xc3\x28"; /* invalid 2-byte start */

static int
test_span_surfaces(void)
{
	otlp_span_t *s;
	otlp_status_t st;

	check_true(otlp_span_create(BAD) == NULL);
	s = otlp_span_create("ok");
	check_true(s != NULL);

	st = otlp_span_set_name(s, BAD);
	check_true(st == OTLP_ERR_UTF8);
	st = otlp_span_set_trace_state(s, BAD);
	check_true(st == OTLP_ERR_UTF8);
	st = otlp_span_set_status(s, OTLP_STATUS_CODE_ERROR, BAD);
	check_true(st == OTLP_ERR_UTF8);
	st = otlp_span_add_event(s, BAD, 0);
	check_true(st == OTLP_ERR_UTF8);
	/* Nothing was appended (opaque type: verified via the rc + the
	 * event test in unit-span). */

	st = otlp_span_set_attribute_string(s, BAD, "v");
	check_true(st == OTLP_ERR_UTF8);
	st = otlp_span_set_attribute_string(s, "k", BAD);
	check_true(st == OTLP_ERR_UTF8);
	/* bytes values are exempt — proto3 bytes accepts anything. */
	{
		static const uint8_t raw[2] = { 0xc3, 0x28 };

		st = otlp_span_set_attribute_bytes(s, "raw", raw, 2);
		check_true(st == OTLP_OK);
	}
	otlp_span_free(s);
	return 0;
}

static int
test_composite_surfaces(void)
{
	otlp_span_t *s = otlp_span_create("ok");
	const otlp_value_t items[1] = { { .type = OTLP_VALUE_STRING,
		.v = { .string_val = BAD } } };
	const otlp_value_t good[1] = { { .type = OTLP_VALUE_STRING,
		.v = { .string_val = "ok" } } };
	const otlp_kv_t kv_bad_value[1] = { { .key = "k",
		.value = { .type = OTLP_VALUE_STRING,
			.v = { .string_val = BAD } } } };
	const otlp_kv_t kv_bad_key[1] = { { .key = BAD,
		.value = { .type = OTLP_VALUE_STRING,
			.v = { .string_val = "v" } } } };
	otlp_status_t st;

	check_true(s != NULL);
	st = otlp_span_set_attribute_array(s, BAD, good, 1);
	check_true(st == OTLP_ERR_UTF8);
	st = otlp_span_set_attribute_array(s, "arr", items, 1);
	check_true(st == OTLP_ERR_UTF8);
	st = otlp_span_set_attribute_kvlist(s, "kvl", kv_bad_value, 1);
	check_true(st == OTLP_ERR_UTF8);
	st = otlp_span_set_attribute_kvlist(s, "kvl", kv_bad_key, 1);
	check_true(st == OTLP_ERR_UTF8);
	/* good composites still pass */
	st = otlp_span_set_attribute_array(s, "arr", good, 1);
	check_true(st == OTLP_OK);
	st = otlp_span_set_attribute_kvlist(s,
		"kvl",
		(const otlp_kv_t[]){ { .key = "k",
			.value = { .type = OTLP_VALUE_STRING,
				.v = { .string_val = "v" } } } },
		1);
	check_true(st == OTLP_OK);
	otlp_span_free(s);
	return 0;
}

static int
test_metric_log_exporter_surfaces(void)
{
	otlp_log_record_t *lr;
	otlp_exporter_opts_t opts;
	otlp_status_t st;

	check_true(
		otlp_metric_create(
			OTLP_METRIC_COUNTER, BAD, "1", "d", NULL, 0) == NULL);
	check_true(
		otlp_metric_create(
			OTLP_METRIC_COUNTER, "n", BAD, "d", NULL, 0) == NULL);
	check_true(
		otlp_metric_create(
			OTLP_METRIC_COUNTER, "n", "1", BAD, NULL, 0) == NULL);

	check_true(otlp_log_record_create(OTLP_SEVERITY_INFO, BAD) == NULL);
	lr = otlp_log_record_create(OTLP_SEVERITY_INFO, "ok");
	check_true(lr != NULL);
	st = otlp_log_record_set_severity_text(lr, BAD);
	check_true(st == OTLP_ERR_UTF8);
	otlp_log_record_free(lr);

	memset(&opts, 0, sizeof(opts));
	opts.service_name = BAD;
	check_true(otlp_exporter_create(&opts) == NULL);
	{
		const otlp_resource_attr_t attr = {
			.key = BAD,
			.value = { .type = OTLP_VALUE_STRING,
				.v = { .string_val = "v" } },
		};

		memset(&opts, 0, sizeof(opts));
		opts.resource_attributes = &attr;
		opts.n_resource_attributes = 1;
		check_true(otlp_exporter_create(&opts) == NULL);
	}
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_validator_valid();
	failures += test_validator_invalid();
	failures += test_span_surfaces();
	failures += test_composite_surfaces();
	failures += test_metric_log_exporter_surfaces();

	if (failures)
		printf("[unit-utf8] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-utf8] PASS (5 tests)\n");
	return failures ? 1 : 0;
}
