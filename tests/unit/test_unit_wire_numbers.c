// SPDX-License-Identifier: Apache-2.0
//
// Known-answer wire test: every field number and wire type in this
// file is a literal copied from opentelemetry-proto (metrics.proto,
// logs.proto — upstream main, verified 2026-08), deliberately NOT
// derived from src/otlp_schema.h. otlp_schema.h is the single
// source of truth for the encoders; this file is the independent
// reference it must match.
//
// Why this exists (v0.5.97): HistogramDataPoint min/max were
// emitted at fields 10/11 for the schema's whole life — upstream is
// min=11, max=12 (10 is `flags`, uint32 varint). No test caught it
// because every existing test walked the wire using the schema's
// own numbers: a self-referential check can only ever agree with
// the bug. The literals here break that circle.
//
// assert() discipline: decoder calls advance the reader, so they
// never appear inside assert() — Release/NDEBUG would elide them
// and desynchronize the walk. Every call is a statement whose
// result feeds a flag; asserts only inspect flags.

#include "otlp_messages.h"
#include "../test_util.h"
#include "otlp_schema.h"
#include "protobuf_decode.h"
#include "protobuf_encode.h"

#include <otlp-c/metric.h>

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Read the CURRENT field as an 8-byte fixed64 (wire type 1). The
 * library's decoder has no fixed-width reader (its only consumer
 * reads PartialSuccess varints/strings), so the test supplies it. */
static bool
read_fixed64_raw(struct otlp_pb_reader *r, uint64_t *v)
{
	if (r->len - r->pos < 8)
		return false;
	memcpy(v, r->buf + r->pos, 8);
	r->pos += 8;
	return true;
}

static uint64_t
bits_of(double d)
{
	uint64_t u;

	memcpy(&u, &d, sizeof(u));
	return u;
}

/* Advance `r` to the wanted field and return its LEN payload.
 * False if the field is absent (or malformed) before end. */
static bool
find_submessage(struct otlp_pb_reader *r,
	uint32_t want_field,
	const uint8_t **sub,
	size_t *sub_len)
{
	for (;;)
	{
		uint32_t field;
		int wt;

		if (!otlp_pb_read_key(r, &field, &wt))
			return false;
		if (field == want_field && wt == OTLP_PB_WIRE_LEN)
			return otlp_pb_read_len(r, sub, sub_len);
		if (!otlp_pb_skip(r, wt))
			return false;
	}
}

/* Descend ExportMetricsServiceRequest → ResourceMetrics →
 * ScopeMetrics → Metric → <oneof field> → data points. All numbers
 * are upstream literals (see file header). */
static bool
find_metric_data_point(const uint8_t *body,
	size_t body_len,
	uint32_t oneof_field,
	const uint8_t **dp,
	size_t *dp_len)
{
	const uint8_t *em, *sm, *met, *kind;
	size_t em_len, sm_len, met_len, kind_len;
	struct otlp_pb_reader r;

	otlp_pb_reader_init(&r, body, body_len);
	if (!find_submessage(&r, 1 /* resource_metrics */, &em, &em_len))
		return false;
	otlp_pb_reader_init(&r, em, em_len);
	if (!find_submessage(&r, 2 /* scope_metrics */, &sm, &sm_len))
		return false;
	otlp_pb_reader_init(&r, sm, sm_len);
	if (!find_submessage(&r, 2 /* metrics */, &met, &met_len))
		return false;
	otlp_pb_reader_init(&r, met, met_len);
	if (!find_submessage(&r, oneof_field, &kind, &kind_len))
		return false;
	otlp_pb_reader_init(&r, kind, kind_len);
	return find_submessage(&r, 1 /* data_points */, dp, dp_len);
}

/* End-to-end: encode a histogram with recorded values and assert
 * every emitted HistogramDataPoint field lands on its upstream
 * number/wire type — min at 11 and max at 12 above all. */
static int
test_histogram_wire_numbers(void)
{
	static const double bounds[2] = { 10.0, 20.0 };
	static const uint64_t want_buckets[3] = { 1, 2, 1 };
	const uint64_t want_bounds[2] = { bits_of(10.0), bits_of(20.0) };
	otlp_metric_t *m;
	const otlp_metric_t *metrics[1];
	struct otlp_pb_buf body = { 0 };
	otlp_status_t st;
	const uint8_t *dp;
	size_t dp_len;
	struct otlp_pb_reader r;
	bool found, have_count = false, have_sum = false;
	bool have_buckets = false, have_bounds = false;
	bool have_min = false, have_max = false, have_field_10 = false;
	bool ok = true;
	uint64_t count_v = 0, sum_bits = 0, min_bits = 0, max_bits = 0;

	m = otlp_metric_create(
		OTLP_METRIC_HISTOGRAM, "latency", "ms", "doc", bounds, 2);
	check_true(m != NULL);
	st = otlp_metric_record(m, 5.0);
	check_true(st == OTLP_OK);
	st = otlp_metric_record(m, 15.0);
	check_true(st == OTLP_OK);
	st = otlp_metric_record(m, 15.0);
	check_true(st == OTLP_OK);
	st = otlp_metric_record(m, 25.0);
	check_true(st == OTLP_OK);
	metrics[0] = m;
	st = otlp_encode_export_metrics_service_request(
		&body, "wire-svc", NULL, 0, NULL, NULL, metrics, 1);
	check_true(st == OTLP_OK);
	otlp_metric_free(m);

	found = find_metric_data_point(
		body.data, body.len, 9 /* histogram */, &dp, &dp_len);
	check_true(found);

	otlp_pb_reader_init(&r, dp, dp_len);
	for (;;)
	{
		uint32_t field;
		int wt;

		if (!otlp_pb_read_key(&r, &field, &wt))
		{
			check_true(
				r.pos >= r.len); /* clean EOF, not malformed */
			break;
		}
		switch (field)
		{
			case 4: /* count, fixed64 */
				if (wt == OTLP_PB_WIRE_FIXED64 &&
					read_fixed64_raw(&r, &count_v))
					have_count = true;
				else
					ok = false;
				break;
			case 5: /* sum, double as fixed64 */
				if (wt == OTLP_PB_WIRE_FIXED64 &&
					read_fixed64_raw(&r, &sum_bits))
					have_sum = true;
				else
					ok = false;
				break;
			case 6: /* bucket_counts, packed fixed64 */
			{
				const uint8_t *packed;
				size_t packed_len, i = 0;
				struct otlp_pb_reader p;

				if (wt != OTLP_PB_WIRE_LEN ||
					!otlp_pb_read_len(
						&r, &packed, &packed_len))
				{
					ok = false;
					break;
				}
				otlp_pb_reader_init(&p, packed, packed_len);
				while (p.pos < p.len)
				{
					uint64_t v;

					if (!read_fixed64_raw(&p, &v) ||
						i >= 3 || v != want_buckets[i])
					{
						ok = false;
						break;
					}
					i++;
				}
				if (i == 3)
					have_buckets = true;
				else
					ok = false;
				break;
			}
			case 7: /* explicit_bounds, packed double */
			{
				const uint8_t *packed;
				size_t packed_len, i = 0;
				struct otlp_pb_reader p;

				if (wt != OTLP_PB_WIRE_LEN ||
					!otlp_pb_read_len(
						&r, &packed, &packed_len))
				{
					ok = false;
					break;
				}
				otlp_pb_reader_init(&p, packed, packed_len);
				while (p.pos < p.len)
				{
					uint64_t v;

					if (!read_fixed64_raw(&p, &v) ||
						i >= 2 || v != want_bounds[i])
					{
						ok = false;
						break;
					}
					i++;
				}
				if (i == 2)
					have_bounds = true;
				else
					ok = false;
				break;
			}
			case 10:
				/* Upstream field 10 is `flags` (uint32 varint),
				 * which this encoder never emits. Any field 10
				 * — under any wire type — means min/max have
				 * drifted back onto the wrong number. */
				have_field_10 = true;
				if (!otlp_pb_skip(&r, wt))
					ok = false;
				break;
			case 11: /* min, double as fixed64 — THE v0.5.97 fix */
				if (wt == OTLP_PB_WIRE_FIXED64 &&
					read_fixed64_raw(&r, &min_bits))
					have_min = true;
				else
					ok = false;
				break;
			case 12: /* max, double as fixed64 — THE v0.5.97 fix */
				if (wt == OTLP_PB_WIRE_FIXED64 &&
					read_fixed64_raw(&r, &max_bits))
					have_max = true;
				else
					ok = false;
				break;
			default:
				/* 2/3 (start/time), 9 (attributes), or an
				 * upstream field this encoder doesn't emit. */
				if (!otlp_pb_skip(&r, wt))
					ok = false;
				break;
		}
		if (!ok)
			break;
	}

	check_true(ok);
	check_true(have_count);
	check_true(count_v == 4);
	check_true(have_sum);
	check_true(sum_bits == bits_of(60.0));
	check_true(have_buckets);
	check_true(have_bounds);
	check_true(have_min);
	check_true(min_bits == bits_of(5.0));
	check_true(have_max);
	check_true(max_bits == bits_of(25.0));
	check_true(!have_field_10);

	otlp_pb_buf_free(&body);
	printf("[unit-wire] histogram min@11 max@12 count@4 sum@5 "
	       "buckets@6 bounds@7\n");
	return 0;
}

/* End-to-end for ExponentialHistogram: scale must zigzag at field
 * 6, positive buckets at 8, and inside them offset=1 (zigzag
 * varint) + bucket_counts=2 (packed varint — unlike HDP's
 * fixed64-packed counts). */
static int
test_exp_histogram_wire_numbers(void)
{
	static const uint64_t pos_counts[2] = { 1, 3 };
	otlp_metric_t *m;
	const otlp_metric_t *metrics[1];
	struct otlp_pb_buf body = { 0 };
	otlp_status_t st;
	const uint8_t *dp;
	size_t dp_len;
	struct otlp_pb_reader r;
	bool found, have_scale = false, have_positive = false;
	bool ok = true;
	uint64_t scale_v = 0;

	m = otlp_metric_create(
		OTLP_METRIC_EXP_HISTOGRAM, "eh", "1", "doc", NULL, 0);
	check_true(m != NULL);
	st = otlp_metric_set_exp_histogram(m, -2, 4, pos_counts, 2, 0, NULL, 0);
	check_true(st == OTLP_OK);
	metrics[0] = m;
	st = otlp_encode_export_metrics_service_request(
		&body, "wire-svc", NULL, 0, NULL, NULL, metrics, 1);
	check_true(st == OTLP_OK);
	otlp_metric_free(m);

	found = find_metric_data_point(body.data,
		body.len,
		10 /* exponential_histogram */,
		&dp,
		&dp_len);
	check_true(found);

	otlp_pb_reader_init(&r, dp, dp_len);
	for (;;)
	{
		uint32_t field;
		int wt;

		if (!otlp_pb_read_key(&r, &field, &wt))
		{
			check_true(r.pos >= r.len);
			break;
		}
		switch (field)
		{
			case 6: /* scale, sint32 → zigzag → varint */
				if (wt == OTLP_PB_WIRE_VARINT &&
					otlp_pb_read_varint(&r, &scale_v))
					have_scale = true;
				else
					ok = false;
				break;
			case 8: /* positive buckets submessage */
			{
				const uint8_t *b;
				size_t b_len;
				struct otlp_pb_reader br;
				bool have_offset = false;
				uint64_t off_v = 0, got_counts[2] = { 0, 0 };
				size_t n_counts = 0;

				if (wt != OTLP_PB_WIRE_LEN ||
					!otlp_pb_read_len(&r, &b, &b_len))
				{
					ok = false;
					break;
				}
				otlp_pb_reader_init(&br, b, b_len);
				for (;;)
				{
					uint32_t f2;
					int wt2;

					if (!otlp_pb_read_key(&br, &f2, &wt2))
					{
						check_true(br.pos >= br.len);
						break;
					}
					if (f2 == 1) /* offset, sint32 zigzag */
					{
						if (wt2 == OTLP_PB_WIRE_VARINT &&
							otlp_pb_read_varint(
								&br, &off_v))
							have_offset = true;
						else
							ok = false;
					}
					else if (f2 ==
						2) /* counts, packed varint */
					{
						const uint8_t *packed;
						size_t packed_len;
						struct otlp_pb_reader pr;

						if (wt2 != OTLP_PB_WIRE_LEN ||
							!otlp_pb_read_len(&br,
								&packed,
								&packed_len))
						{
							ok = false;
							break;
						}
						otlp_pb_reader_init(&pr,
							packed,
							packed_len);
						while (pr.pos < pr.len)
						{
							uint64_t v;

							if (!otlp_pb_read_varint(
								    &pr, &v) ||
								n_counts >= 2)
							{
								ok = false;
								break;
							}
							got_counts[n_counts++] =
								v;
						}
					}
					else
					{
						if (!otlp_pb_skip(&br, wt2))
							ok = false;
					}
					if (!ok)
						break;
				}
				if (ok && have_offset && n_counts == 2 &&
					off_v == 8 /* zigzag(4) */
					&& got_counts[0] == 1 &&
					got_counts[1] == 3)
					have_positive = true;
				else
					ok = false;
				break;
			}
			default:
				if (!otlp_pb_skip(&r, wt))
					ok = false;
				break;
		}
		if (!ok)
			break;
	}

	check_true(ok);
	check_true(have_scale);
	check_true(scale_v == 3); /* zigzag(-2) */
	check_true(have_positive);

	otlp_pb_buf_free(&body);
	printf("[unit-wire] exp-histogram scale@6 buckets@8 "
	       "(offset@1 zigzag, counts@2 varint)\n");
	return 0;
}

/* Pin the schema tables themselves against upstream literals. The
 * wire tests above cover what today's encoders emit; this catches
 * dormant entries (fields declared but not yet emitted, e.g. EHDP
 * flags) drifting from upstream. */
static int
test_schema_pins_upstream(void)
{
	check_true(OTLP_NDP_FIELDS[OTLP_NDP_FI_START_TIME].number == 2);
	check_true(OTLP_NDP_FIELDS[OTLP_NDP_FI_TIME].number == 3);
	check_true(OTLP_NDP_FIELDS[OTLP_NDP_FI_AS_DOUBLE].number == 4);
	check_true(OTLP_NDP_FIELDS[OTLP_NDP_FI_AS_DOUBLE].wire_type ==
		OTLP_PB_WIRE_FIXED64);
	check_true(OTLP_NDP_FIELDS[OTLP_NDP_FI_ATTRIBUTES].number == 7);

	check_true(OTLP_HDP_FIELDS[OTLP_HDP_FI_COUNT].number == 4);
	check_true(OTLP_HDP_FIELDS[OTLP_HDP_FI_SUM].number == 5);
	check_true(OTLP_HDP_FIELDS[OTLP_HDP_FI_BUCKET_COUNTS].number == 6);
	check_true(OTLP_HDP_FIELDS[OTLP_HDP_FI_EXPLICIT_BOUNDS].number == 7);
	check_true(OTLP_HDP_FIELDS[OTLP_HDP_FI_ATTRIBUTES].number == 9);
	check_true(OTLP_HDP_FIELDS[OTLP_HDP_FI_MIN].number == 11);
	check_true(OTLP_HDP_FIELDS[OTLP_HDP_FI_MIN].wire_type ==
		OTLP_PB_WIRE_FIXED64);
	check_true(OTLP_HDP_FIELDS[OTLP_HDP_FI_MAX].number == 12);
	check_true(OTLP_HDP_FIELDS[OTLP_HDP_FI_MAX].wire_type ==
		OTLP_PB_WIRE_FIXED64);

	check_true(OTLP_EHDP_FIELDS[OTLP_EHDP_FI_ATTRIBUTES].number == 1);
	check_true(OTLP_EHDP_FIELDS[OTLP_EHDP_FI_SCALE].number == 6);
	check_true(OTLP_EHDP_FIELDS[OTLP_EHDP_FI_SCALE].wire_type ==
		OTLP_PB_WIRE_VARINT);
	check_true(OTLP_EHDP_FIELDS[OTLP_EHDP_FI_ZERO_COUNT].number == 7);
	check_true(OTLP_EHDP_FIELDS[OTLP_EHDP_FI_POSITIVE].number == 8);
	check_true(OTLP_EHDP_FIELDS[OTLP_EHDP_FI_NEGATIVE].number == 9);
	/* Upstream: uint32 flags = 10 — varint, NOT fixed32. */
	check_true(OTLP_EHDP_FIELDS[OTLP_EHDP_FI_FLAGS].number == 10);
	check_true(OTLP_EHDP_FIELDS[OTLP_EHDP_FI_FLAGS].wire_type ==
		OTLP_PB_WIRE_VARINT);

	check_true(OTLP_EHB_FIELDS[OTLP_EHB_FI_OFFSET].number == 1);
	check_true(OTLP_EHB_FIELDS[OTLP_EHB_FI_BUCKET_COUNTS].number == 2);

	check_true(OTLP_METRIC_FIELDS[OTLP_METRIC_FI_GAUGE].number == 5);
	check_true(OTLP_METRIC_FIELDS[OTLP_METRIC_FI_SUM].number == 7);
	check_true(OTLP_METRIC_FIELDS[OTLP_METRIC_FI_HISTOGRAM].number == 9);
	check_true(
		OTLP_METRIC_FIELDS[OTLP_METRIC_FI_EXP_HISTOGRAM].number == 10);

	/* LogRecord flags IS fixed32 upstream (fixed32 flags = 8) —
	 * pinned so nobody "harmonizes" it with EHDP's varint flags. */
	check_true(OTLP_LOG_FIELDS[OTLP_LOG_FI_FLAGS].number == 8);
	check_true(OTLP_LOG_FIELDS[OTLP_LOG_FI_FLAGS].wire_type ==
		OTLP_PB_WIRE_FIXED32);

	printf("[unit-wire] schema tables match upstream literals\n");
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_histogram_wire_numbers();
	failures += test_exp_histogram_wire_numbers();
	failures += test_schema_pins_upstream();

	if (failures)
		printf("[unit-wire] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-wire] PASS (3 tests)\n");

	return failures ? 1 : 0;
}
