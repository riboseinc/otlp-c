// SPDX-License-Identifier: BSD-3-Clause
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

#include <otlp-c/log.h>
#include <otlp-c/metric.h>
#include <otlp-c/span.h>

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

/* Like find_submessage, but for a FIXED64 field: advance to it
 * and read the 8 raw bytes. */
static bool
find_field_fixed64(struct otlp_pb_reader *r,
	uint32_t want_field,
	uint64_t *v)
{
	for (;;)
	{
		uint32_t field;
		int wt;

		if (!otlp_pb_read_key(r, &field, &wt))
			return false;
		if (field == want_field && wt == OTLP_PB_WIRE_FIXED64)
			return read_fixed64_raw(r, v);
		if (!otlp_pb_skip(r, wt))
			return false;
	}
}

/* Like find_submessage, but for a LEN field read as raw bytes. */
static bool
find_field_bytes(struct otlp_pb_reader *r,
	uint32_t want_field,
	const uint8_t **b,
	size_t *b_len)
{
	return find_submessage(r, want_field, b, b_len);
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
		&body, "wire-svc", NULL, NULL, 0, NULL, NULL, metrics, 1);
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
		&body, "wire-svc", NULL, NULL, 0, NULL, NULL, metrics, 1);
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

/* ── Schema pins: every table vs upstream literals ──────────
 *
 * One pin per emitted field of every table in otlp_schema.h.
 * The numbers/wire types below are copied from the
 * opentelemetry-proto .proto files — NEVER derived from
 * otlp_schema.h: a self-referential check can only agree with
 * the schema, bug included (that circle is how the HDP min/max
 * drift survived; v0.5.97). */
struct wire_pin
{
	int fi;
	uint32_t number;
	int wire;
};

static void
pin_table(const char *table,
	const struct otlp_field_spec *fields,
	const struct wire_pin *pins,
	size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
	{
		if (fields[pins[i].fi].number != pins[i].number ||
			fields[pins[i].fi].wire_type != pins[i].wire)
		{
			fprintf(stderr,
				"pin FAILED: %s: schema has "
				"{number=%u, wire=%d}, "
				"upstream literal is {number=%u, wire=%d}\n",
				table,
				fields[pins[i].fi].number,
				fields[pins[i].fi].wire_type,
				pins[i].number,
				pins[i].wire);
			abort();
		}
	}
}

#define N_PINS(a) (sizeof(a) / sizeof((a)[0]))

static const struct wire_pin PINS_EX[] = {
	/* Literals from the installed opentelemetry-proto descriptor
	 * (v1.0.1: the v0.8.0 literals were hand-copied wrong). */
	{ OTLP_EX_FI_TIME, 2, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_EX_FI_DOUBLE_VALUE, 3, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_EX_FI_SPAN_ID, 4, OTLP_PB_WIRE_LEN },
	{ OTLP_EX_FI_TRACE_ID, 5, OTLP_PB_WIRE_LEN },
	{ OTLP_EX_FI_INT_VALUE, 6, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_EX_FI_FILTERED_ATTRIBUTES, 7, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_ETSR[] = {
	{ OTLP_ETSR_FI_RESOURCE_SPANS, 1, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_RS[] = {
	{ OTLP_RS_FI_RESOURCE, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_RS_FI_SCOPE_SPANS, 2, OTLP_PB_WIRE_LEN },
	{ OTLP_RS_FI_SCHEMA_URL, 3, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_R[] = {
	{ OTLP_R_FI_ATTRIBUTES, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_R_FI_DROPPED_ATTRS, 2, OTLP_PB_WIRE_VARINT }
};

static const struct wire_pin PINS_SS[] = {
	{ OTLP_SS_FI_SCOPE, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_SS_FI_SPANS, 2, OTLP_PB_WIRE_LEN },
	{ OTLP_SS_FI_SCHEMA_URL, 3, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_IS[] = {
	{ OTLP_IS_FI_NAME, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_IS_FI_VERSION, 2, OTLP_PB_WIRE_LEN },
	{ OTLP_IS_FI_ATTRIBUTES, 3, OTLP_PB_WIRE_LEN },
	{ OTLP_IS_FI_DROPPED_ATTRS, 4, OTLP_PB_WIRE_VARINT }
};

static const struct wire_pin PINS_SPAN[] = {
	{ OTLP_SPAN_FI_TRACE_ID, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_SPAN_FI_SPAN_ID, 2, OTLP_PB_WIRE_LEN },
	{ OTLP_SPAN_FI_TRACE_STATE, 3, OTLP_PB_WIRE_LEN },
	{ OTLP_SPAN_FI_PARENT_SPAN_ID, 4, OTLP_PB_WIRE_LEN },
	{ OTLP_SPAN_FI_NAME, 5, OTLP_PB_WIRE_LEN },
	{ OTLP_SPAN_FI_KIND, 6, OTLP_PB_WIRE_VARINT },
	{ OTLP_SPAN_FI_START_TIME, 7, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_SPAN_FI_END_TIME, 8, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_SPAN_FI_ATTRIBUTES, 9, OTLP_PB_WIRE_LEN },
	{ OTLP_SPAN_FI_DROPPED_ATTRS, 10, OTLP_PB_WIRE_VARINT },
	{ OTLP_SPAN_FI_EVENTS, 11, OTLP_PB_WIRE_LEN },
	{ OTLP_SPAN_FI_DROPPED_EVENTS, 12, OTLP_PB_WIRE_VARINT },
	{ OTLP_SPAN_FI_LINKS, 13, OTLP_PB_WIRE_LEN },
	{ OTLP_SPAN_FI_DROPPED_LINKS, 14, OTLP_PB_WIRE_VARINT },
	{ OTLP_SPAN_FI_STATUS, 15, OTLP_PB_WIRE_LEN },
	{ OTLP_SPAN_FI_FLAGS, 16, OTLP_PB_WIRE_FIXED32 }
};

static const struct wire_pin PINS_STATUS[] = {
	{ OTLP_STATUS_FI_MESSAGE, 2, OTLP_PB_WIRE_LEN },
	{ OTLP_STATUS_FI_CODE, 3, OTLP_PB_WIRE_VARINT }
};

static const struct wire_pin PINS_KV[] = {
	{ OTLP_KV_FI_KEY, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_KV_FI_VALUE, 2, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_AV[] = {
	{ OTLP_AV_FI_STRING, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_AV_FI_BOOL, 2, OTLP_PB_WIRE_VARINT },
	{ OTLP_AV_FI_INT64, 3, OTLP_PB_WIRE_VARINT },
	{ OTLP_AV_FI_DOUBLE, 4, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_AV_FI_ARRAY_VALUE, 5, OTLP_PB_WIRE_LEN },
	{ OTLP_AV_FI_KVLIST_VALUE, 6, OTLP_PB_WIRE_LEN },
	{ OTLP_AV_FI_BYTES, 7, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_AV_ARRAY[] = {
	{ OTLP_AV_ARRAY_FI_VALUES, 1, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_KVLIST[] = {
	{ OTLP_KVLIST_FI_VALUES, 1, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_EVENT[] = {
	{ OTLP_EVENT_FI_TIME, 1, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_EVENT_FI_NAME, 2, OTLP_PB_WIRE_LEN },
	{ OTLP_EVENT_FI_ATTRIBUTES, 3, OTLP_PB_WIRE_LEN },
	{ OTLP_EVENT_FI_DROPPED_ATTRS, 4, OTLP_PB_WIRE_VARINT }
};

static const struct wire_pin PINS_LINK[] = {
	{ OTLP_LINK_FI_TRACE_ID, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_LINK_FI_SPAN_ID, 2, OTLP_PB_WIRE_LEN },
	{ OTLP_LINK_FI_TRACE_STATE, 3, OTLP_PB_WIRE_LEN },
	{ OTLP_LINK_FI_ATTRIBUTES, 4, OTLP_PB_WIRE_LEN },
	{ OTLP_LINK_FI_DROPPED_ATTRS, 5, OTLP_PB_WIRE_VARINT },
	{ OTLP_LINK_FI_FLAGS, 6, OTLP_PB_WIRE_FIXED32 }
};

static const struct wire_pin PINS_EMSR[] = {
	{ OTLP_EMSR_FI_RESOURCE_METRICS, 1, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_RM[] = {
	{ OTLP_RM_FI_RESOURCE, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_RM_FI_SCOPE_METRICS, 2, OTLP_PB_WIRE_LEN },
	{ OTLP_RM_FI_SCHEMA_URL, 3, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_SM[] = {
	{ OTLP_SM_FI_SCOPE, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_SM_FI_METRICS, 2, OTLP_PB_WIRE_LEN },
	{ OTLP_SM_FI_SCHEMA_URL, 3, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_METRIC[] = {
	{ OTLP_METRIC_FI_NAME, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_METRIC_FI_DESCRIPTION, 2, OTLP_PB_WIRE_LEN },
	{ OTLP_METRIC_FI_UNIT, 3, OTLP_PB_WIRE_LEN },
	{ OTLP_METRIC_FI_GAUGE, 5, OTLP_PB_WIRE_LEN },
	{ OTLP_METRIC_FI_SUM, 7, OTLP_PB_WIRE_LEN },
	{ OTLP_METRIC_FI_HISTOGRAM, 9, OTLP_PB_WIRE_LEN },
	{ OTLP_METRIC_FI_EXP_HISTOGRAM, 10, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_SUM[] = {
	{ OTLP_SUM_FI_DATA_POINTS, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_SUM_FI_AGG_TEMP, 2, OTLP_PB_WIRE_VARINT },
	{ OTLP_SUM_FI_IS_MONOTONIC, 3, OTLP_PB_WIRE_VARINT }
};

static const struct wire_pin PINS_GAUGE[] = {
	{ OTLP_GAUGE_FI_DATA_POINTS, 1, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_HIST[] = {
	{ OTLP_HIST_FI_DATA_POINTS, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_HIST_FI_AGG_TEMP, 2, OTLP_PB_WIRE_VARINT }
};

static const struct wire_pin PINS_EH[] = {
	{ OTLP_EH_FI_DATA_POINTS, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_EH_FI_AGG_TEMP, 2, OTLP_PB_WIRE_VARINT }
};

static const struct wire_pin PINS_NDP[] = {
	{ OTLP_NDP_FI_START_TIME, 2, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_NDP_FI_TIME, 3, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_NDP_FI_AS_DOUBLE, 4, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_NDP_FI_ATTRIBUTES, 7, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_HDP[] = {
	{ OTLP_HDP_FI_START_TIME, 2, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_HDP_FI_TIME, 3, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_HDP_FI_COUNT, 4, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_HDP_FI_SUM, 5, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_HDP_FI_BUCKET_COUNTS, 6, OTLP_PB_WIRE_LEN },
	{ OTLP_HDP_FI_EXPLICIT_BOUNDS, 7, OTLP_PB_WIRE_LEN },
	{ OTLP_HDP_FI_ATTRIBUTES, 9, OTLP_PB_WIRE_LEN },
	{ OTLP_HDP_FI_MIN, 11, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_HDP_FI_MAX, 12, OTLP_PB_WIRE_FIXED64 }
};

static const struct wire_pin PINS_EHDP[] = {
	{ OTLP_EHDP_FI_ATTRIBUTES, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_EHDP_FI_START_TIME, 2, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_EHDP_FI_TIME, 3, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_EHDP_FI_COUNT, 4, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_EHDP_FI_SUM, 5, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_EHDP_FI_SCALE, 6, OTLP_PB_WIRE_VARINT },
	{ OTLP_EHDP_FI_ZERO_COUNT, 7, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_EHDP_FI_POSITIVE, 8, OTLP_PB_WIRE_LEN },
	{ OTLP_EHDP_FI_NEGATIVE, 9, OTLP_PB_WIRE_LEN },
	{ OTLP_EHDP_FI_FLAGS, 10, OTLP_PB_WIRE_VARINT }
};

static const struct wire_pin PINS_EHB[] = {
	{ OTLP_EHB_FI_OFFSET, 1, OTLP_PB_WIRE_VARINT },
	{ OTLP_EHB_FI_BUCKET_COUNTS, 2, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_ELSR[] = {
	{ OTLP_ELSR_FI_RESOURCE_LOGS, 1, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_RL[] = {
	{ OTLP_RL_FI_RESOURCE, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_RL_FI_SCOPE_LOGS, 2, OTLP_PB_WIRE_LEN },
	{ OTLP_RL_FI_SCHEMA_URL, 3, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_SL[] = {
	{ OTLP_SL_FI_SCOPE, 1, OTLP_PB_WIRE_LEN },
	{ OTLP_SL_FI_LOG_RECORDS, 2, OTLP_PB_WIRE_LEN },
	{ OTLP_SL_FI_SCHEMA_URL, 3, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_LOG[] = {
	{ OTLP_LOG_FI_TIME, 1, OTLP_PB_WIRE_FIXED64 },
	{ OTLP_LOG_FI_SEVERITY_NUMBER, 2, OTLP_PB_WIRE_VARINT },
	{ OTLP_LOG_FI_SEVERITY_TEXT, 3, OTLP_PB_WIRE_LEN },
	{ OTLP_LOG_FI_BODY, 5, OTLP_PB_WIRE_LEN },
	{ OTLP_LOG_FI_ATTRIBUTES, 6, OTLP_PB_WIRE_LEN },
	{ OTLP_LOG_FI_DROPPED_ATTRS, 7, OTLP_PB_WIRE_VARINT },
	{ OTLP_LOG_FI_FLAGS, 8, OTLP_PB_WIRE_FIXED32 },
	{ OTLP_LOG_FI_TRACE_ID, 9, OTLP_PB_WIRE_LEN },
	{ OTLP_LOG_FI_SPAN_ID, 10, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_EXPSR[] = {
	{ OTLP_EXPSR_FI_PARTIAL_SUCCESS, 1, OTLP_PB_WIRE_LEN }
};

static const struct wire_pin PINS_EPS[] = {
	{ OTLP_EPS_FI_REJECTED, 1, OTLP_PB_WIRE_VARINT },
	{ OTLP_EPS_FI_ERROR_MESSAGE, 2, OTLP_PB_WIRE_LEN }
};

static int
test_schema_pins_upstream(void)
{
	pin_table("EX", OTLP_EX_FIELDS, PINS_EX, N_PINS(PINS_EX));
	pin_table("ETSR", OTLP_ETSR_FIELDS, PINS_ETSR, N_PINS(PINS_ETSR));
	pin_table("RS", OTLP_RS_FIELDS, PINS_RS, N_PINS(PINS_RS));
	pin_table("R", OTLP_R_FIELDS, PINS_R, N_PINS(PINS_R));
	pin_table("SS", OTLP_SS_FIELDS, PINS_SS, N_PINS(PINS_SS));
	pin_table("IS", OTLP_IS_FIELDS, PINS_IS, N_PINS(PINS_IS));
	pin_table("SPAN", OTLP_SPAN_FIELDS, PINS_SPAN, N_PINS(PINS_SPAN));
	pin_table(
		"STATUS", OTLP_STATUS_FIELDS, PINS_STATUS, N_PINS(PINS_STATUS));
	pin_table("KV", OTLP_KV_FIELDS, PINS_KV, N_PINS(PINS_KV));
	pin_table("AV", OTLP_AV_FIELDS, PINS_AV, N_PINS(PINS_AV));
	pin_table("AV_ARRAY",
		OTLP_AV_ARRAY_FIELDS,
		PINS_AV_ARRAY,
		N_PINS(PINS_AV_ARRAY));
	pin_table(
		"KVLIST", OTLP_KVLIST_FIELDS, PINS_KVLIST, N_PINS(PINS_KVLIST));
	pin_table("EVENT", OTLP_EVENT_FIELDS, PINS_EVENT, N_PINS(PINS_EVENT));
	pin_table("LINK", OTLP_LINK_FIELDS, PINS_LINK, N_PINS(PINS_LINK));
	pin_table("EMSR", OTLP_EMSR_FIELDS, PINS_EMSR, N_PINS(PINS_EMSR));
	pin_table("RM", OTLP_RM_FIELDS, PINS_RM, N_PINS(PINS_RM));
	pin_table("SM", OTLP_SM_FIELDS, PINS_SM, N_PINS(PINS_SM));
	pin_table(
		"METRIC", OTLP_METRIC_FIELDS, PINS_METRIC, N_PINS(PINS_METRIC));
	pin_table("SUM", OTLP_SUM_FIELDS, PINS_SUM, N_PINS(PINS_SUM));
	pin_table("GAUGE", OTLP_GAUGE_FIELDS, PINS_GAUGE, N_PINS(PINS_GAUGE));
	pin_table("HIST", OTLP_HIST_FIELDS, PINS_HIST, N_PINS(PINS_HIST));
	pin_table("EH", OTLP_EH_FIELDS, PINS_EH, N_PINS(PINS_EH));
	pin_table("NDP", OTLP_NDP_FIELDS, PINS_NDP, N_PINS(PINS_NDP));
	pin_table("HDP", OTLP_HDP_FIELDS, PINS_HDP, N_PINS(PINS_HDP));
	pin_table("EHDP", OTLP_EHDP_FIELDS, PINS_EHDP, N_PINS(PINS_EHDP));
	pin_table("EHB", OTLP_EHB_FIELDS, PINS_EHB, N_PINS(PINS_EHB));
	pin_table("ELSR", OTLP_ELSR_FIELDS, PINS_ELSR, N_PINS(PINS_ELSR));
	pin_table("RL", OTLP_RL_FIELDS, PINS_RL, N_PINS(PINS_RL));
	pin_table("SL", OTLP_SL_FIELDS, PINS_SL, N_PINS(PINS_SL));
	pin_table("LOG", OTLP_LOG_FIELDS, PINS_LOG, N_PINS(PINS_LOG));
	pin_table("EXPSR", OTLP_EXPSR_FIELDS, PINS_EXPSR, N_PINS(PINS_EXPSR));
	pin_table("EPS", OTLP_EPS_FIELDS, PINS_EPS, N_PINS(PINS_EPS));

	printf("[unit-wire] all 31 schema tables pinned against upstream\n");
	return 0;
}

/* v0.7.4: opts.schema_url is emitted as field 3 (upstream
 * literal) on every signal's resource-level message. */
static int
test_schema_url_emitted(void)
{
	static const char URL[] = "https://schema.example.com/1.1.0";
	struct otlp_pb_buf buf = { 0 };
	const uint8_t *rs;
	size_t rs_len;
	struct otlp_pb_reader r;
	const uint8_t *sub;
	size_t sub_len;
	const otlp_span_t *spans[1] = { NULL };
	const otlp_metric_t *mets[1] = { NULL };
	const otlp_log_record_t *logs[1] = { NULL };
	/* (bodies asserted in place; no cross-signal capture needed) */
	otlp_span_t *sp = otlp_span_create("schema-url-probe");

	check_true(sp != NULL);
	spans[0] = sp;
	otlp_span_mark_end(sp);
	mets[0] = otlp_metric_create(
		OTLP_METRIC_COUNTER, "probe", "1", NULL, NULL, 0);
	logs[0] = otlp_log_record_create(OTLP_SEVERITY_INFO, "m");

	/* Traces. */
	check_ok(otlp_pb_buf_init(&buf, 0));
	check_ok(otlp_encode_export_trace_service_request(&buf,
		"svc",
		"https://schema.example.com/1.1.0",
		NULL,
		0,
		NULL,
		NULL,
		spans,
		1));
	otlp_pb_reader_init(&r, buf.data, buf.len);
	check_true(find_submessage(&r, 1, &rs, &rs_len));
	otlp_pb_reader_init(&r, rs, rs_len);
	check_true(find_submessage(&r, 3, &sub, &sub_len));
	check_true(
		sub_len == sizeof(URL) - 1 && memcmp(sub, URL, sub_len) == 0);
	otlp_pb_buf_free(&buf);

	/* Metrics. */
	check_ok(otlp_pb_buf_init(&buf, 0));
	check_ok(otlp_encode_export_metrics_service_request(&buf,
		"svc",
		"https://schema.example.com/1.1.0",
		NULL,
		0,
		NULL,
		NULL,
		mets,
		1));
	otlp_pb_reader_init(&r, buf.data, buf.len);
	check_true(find_submessage(&r, 1, &rs, &rs_len));
	otlp_pb_reader_init(&r, rs, rs_len);
	check_true(find_submessage(&r, 3, &sub, &sub_len));
	check_true(
		sub_len == sizeof(URL) - 1 && memcmp(sub, URL, sub_len) == 0);
	otlp_pb_buf_free(&buf);

	/* Logs. */
	check_ok(otlp_pb_buf_init(&buf, 0));
	check_ok(otlp_encode_export_logs_service_request(&buf,
		"svc",
		"https://schema.example.com/1.1.0",
		NULL,
		0,
		NULL,
		NULL,
		logs,
		1));
	otlp_pb_reader_init(&r, buf.data, buf.len);
	check_true(find_submessage(&r, 1, &rs, &rs_len));
	otlp_pb_reader_init(&r, rs, rs_len);
	check_true(find_submessage(&r, 3, &sub, &sub_len));
	check_true(
		sub_len == sizeof(URL) - 1 && memcmp(sub, URL, sub_len) == 0);
	otlp_pb_buf_free(&buf);

	/* Absent when NULL: ResourceSpans must not carry field 3. */
	check_ok(otlp_pb_buf_init(&buf, 0));
	check_ok(otlp_encode_export_trace_service_request(
		&buf, "svc", NULL, NULL, 0, NULL, NULL, spans, 1));
	otlp_pb_reader_init(&r, buf.data, buf.len);
	check_true(find_submessage(&r, 1, &rs, &rs_len));
	otlp_pb_reader_init(&r, rs, rs_len);
	check_true(!find_submessage(&r, 3, &sub, &sub_len));
	otlp_pb_buf_free(&buf);

	otlp_metric_free((otlp_metric_t *) mets[0]);
	otlp_log_record_free((otlp_log_record_t *) logs[0]);
	otlp_span_free(sp);
	return 0;
}

/* v0.8.0: exemplars — wire-pinned emission on NumberDataPoint
 * (field 5) and HistogramDataPoint (field 8), with the exemplar's
 * own fields at their upstream literals. */
static int
test_exemplars_emitted(void)
{
	otlp_metric_t *m = otlp_metric_create(
		OTLP_METRIC_COUNTER, "exprobe", "1", NULL, NULL, 0);
	otlp_exemplar_t *ex = otlp_exemplar_create();
	const uint8_t trace[16] = { 1 };
	const uint8_t span[8] = { 2 };
	struct otlp_pb_buf buf = { 0 };
	const uint8_t *dp, *exb;
	size_t dp_len, exb_len;
	struct otlp_pb_reader r;
	uint64_t v;
	const uint8_t *b;
	size_t b_len;
	const otlp_metric_t *mets[1];

	check_true(m && ex);
	check_ok(otlp_exemplar_set_double_value(ex, 3.5));
	check_ok(otlp_exemplar_set_trace_context(ex, trace, span));
	/* Timestamp via the API; captured below from the wire. */
	check_ok(otlp_exemplar_mark_time(ex));
	check_ok(otlp_metric_add_exemplar(m, ex));
	otlp_exemplar_free(ex);
	otlp_metric_record(m, 7.0);
	mets[0] = m;

	check_ok(otlp_pb_buf_init(&buf, 0));
	check_ok(otlp_encode_export_metrics_service_request(&buf, "svc",
		NULL, NULL, 0, NULL, NULL, mets, 1));
	check_true(find_metric_data_point(
		buf.data, buf.len, 7 /* Sum oneof */, &dp, &dp_len));
	otlp_pb_reader_init(&r, dp, dp_len);
	check_true(find_submessage(&r, 5 /* exemplars */, &exb, &exb_len));
	{
		struct otlp_pb_reader er;

		otlp_pb_reader_init(&er, exb, exb_len);
		check_true(find_field_fixed64(&er, 3, &v));
		check_true(bits_of(3.5) == v);
		otlp_pb_reader_init(&er, exb, exb_len);
		check_true(find_field_bytes(&er, 5, &b, &b_len));
		check_true(b_len == 16 && b[0] == 1);
		otlp_pb_reader_init(&er, exb, exb_len);
		check_true(find_field_bytes(&er, 4, &b, &b_len));
		check_true(b_len == 8 && b[0] == 2);
		otlp_pb_reader_init(&er, exb, exb_len);
		check_true(find_field_fixed64(&er, 2, &v));
		check_true(v > 0); /* stamped, not absent */
	}
	otlp_pb_buf_free(&buf);
	otlp_metric_free(m);
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_histogram_wire_numbers();
	failures += test_exp_histogram_wire_numbers();
	failures += test_schema_pins_upstream();
	failures += test_schema_url_emitted();
	failures += test_exemplars_emitted();

	if (failures)
		printf("[unit-wire] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-wire] PASS (3 tests)\n");

	return failures ? 1 : 0;
}
