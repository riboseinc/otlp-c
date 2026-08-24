/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Property tests for the OTLP metrics encoder.
 *
 * Walks the wire one level at a time and asserts the OTLP envelope:
 *   ExportMetricsServiceRequest { ResourceMetrics{1} }
 *     → ResourceMetrics { Resource{1}, ScopeMetrics{2} }
 *       → ScopeMetrics { Scope{1}, Metric{2} }
 *         → Metric { name{1}, Sum{5}|Gauge{5}|Histogram{9} }
 *
 * Tests:
 *   prop_metrics_empty_request        — no metrics + no service → 0 bytes.
 *   prop_metrics_counter_field_nums   — counter envelope + Sum fields.
 *   prop_metrics_gauge_field_nums     — gauge envelope + as_double field.
 *   prop_metrics_histogram_field_nums — histogram + count/sum/buckets/bounds.
 *   prop_metrics_counter_value        — encoded as_double == recorded value.
 *   prop_metrics_attributes_roundtrip — typed attribute (int64/double/bool/bytes) as KeyValue oneof member.
 */
#include "decoder.h"
#include "prng.h"
#include "property_harness.h"

#include "../src/otlp_messages.h"
#include "../src/protobuf_encode.h"

#include <otlp-c/metric.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ── Single-level walker helpers ──────────────────────────────── */

/* Find a field at the current level only (no recursion).
 * Returns 1 if found; writes wt + value position + value length.
 * For VARINT, value length is the encoded byte count.
 * For FIXED64, value length is 8.
 * For FIXED32, value length is 4.
 * For LEN, value length is the payload length (excludes length prefix). */
static int
find_at_level(const uint8_t *data,
	size_t pos,
	size_t end,
	uint32_t fnum,
	int *wt_out,
	size_t *val_pos,
	size_t *val_len)
{
	while (pos < end)
	{
		uint32_t fn = 0;
		int wt = 0;
		size_t vstart;
		size_t vlen = 0;
		otlp_status_t st = decode_tag(data, end, &pos, &fn, &wt);

		if (st != OTLP_OK)
			return 0;
		vstart = pos;
		if (wt == OTLP_PB_WIRE_VARINT)
		{
			uint64_t v;

			if (decode_varint(data, end, &pos, &v) != OTLP_OK)
				return 0;
			vlen = pos - vstart;
		}
		else if (wt == OTLP_PB_WIRE_FIXED64)
		{
			if (pos + 8 > end)
				return 0;
			pos += 8;
			vlen = 8;
		}
		else if (wt == OTLP_PB_WIRE_FIXED32)
		{
			if (pos + 4 > end)
				return 0;
			pos += 4;
			vlen = 4;
		}
		else if (wt == OTLP_PB_WIRE_LEN)
		{
			uint64_t l;

			if (decode_varint(data, end, &pos, &l) != OTLP_OK)
				return 0;
			vlen = (size_t) l;
			vstart = pos;
			if (pos + vlen > end)
				return 0;
			pos += vlen;
		}
		else
		{
			return 0;
		}
		if (fn == fnum)
		{
			if (wt_out && val_pos && val_len)
			{
				*wt_out = wt;
				*val_pos = vstart;
				*val_len = vlen;
			}
			return 1;
		}
	}
	return 0;
}

/* Convenience: descend through one LEN field. */
static int
descend(const uint8_t *data, size_t *pos, size_t *end, uint32_t fnum)
{
	int wt = 0;
	size_t vp = 0;
	size_t vl = 0;

	if (!find_at_level(data, *pos, *end, fnum, &wt, &vp, &vl))
		return 0;
	if (wt != OTLP_PB_WIRE_LEN)
		return 0;
	*pos = vp;
	*end = vp + vl;
	return 1;
}

/* ── Properties ───────────────────────────────────────────────── */

static int
prop_metrics_empty_request(uint64_t seed)
{
	struct otlp_pb_buf buf = { 0 };
	int ok = 0;

	(void) seed;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	if (otlp_encode_export_metrics_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, NULL, 0) == OTLP_OK)
		ok = (buf.len == 0);
	otlp_pb_buf_free(&buf);
	return ok;
}

/* Walk: EMSR{1} → RM → SM{2} → Metric{2} → name{1} + Sum{5}. */
static int
prop_metrics_counter_field_nums(uint64_t seed)
{
	otlp_metric_t *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	int ok = 0;
	size_t pos, end;
	int wt;
	size_t vp, vl;
	int saw_name = 0, saw_sum = 0, saw_dp = 0;
	int saw_at = 0, saw_im = 0;

	(void) seed;
	m = otlp_metric_create(
		OTLP_METRIC_COUNTER, "requests", "1", "count", NULL, 0);
	if (!m)
		return 0;
	otlp_metric_record(m, 5.0);
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, "svc", NULL, 0, "scope", "1.0", arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1)) /* ResourceMetrics */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 2)) /* ScopeMetrics */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 2)) /* Metric */
		goto out_buf;

	if (find_at_level(buf.data, pos, end, 1, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_LEN)
		saw_name = 1;
	if (find_at_level(buf.data, pos, end, 7, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_LEN)
	{
		size_t sp = vp, se = vp + vl;

		saw_sum = 1;
		/* Inside Sum: data_points{1}, agg_temp{2}, is_monotonic{3}. */
		if (find_at_level(buf.data, sp, se, 1, &wt, &vp, &vl) &&
			wt == OTLP_PB_WIRE_LEN)
			saw_dp = 1;
		if (find_at_level(buf.data, sp, se, 2, &wt, &vp, &vl) &&
			wt == OTLP_PB_WIRE_VARINT)
			saw_at = 1;
		if (find_at_level(buf.data, sp, se, 3, &wt, &vp, &vl) &&
			wt == OTLP_PB_WIRE_VARINT)
			saw_im = 1;
	}
	ok = saw_name && saw_sum && saw_dp && saw_at && saw_im;

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_metric_free(m);
	return ok;
}

/* Walk: EMSR{1} → RM → SM{2} → Metric{2} → Gauge{5} → DataPoint → as_double{4}.
 */
static int
prop_metrics_gauge_field_nums(uint64_t seed)
{
	otlp_metric_t *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	int ok = 0;
	size_t pos, end;
	int wt;
	size_t vp, vl;

	(void) seed;
	m = otlp_metric_create(
		OTLP_METRIC_GAUGE, "temperature", "C", "", NULL, 0);
	if (!m)
		return 0;
	otlp_metric_record(m, 42.5);
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1)) /* ResourceMetrics */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 2)) /* ScopeMetrics */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 2)) /* Metric */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 5)) /* Gauge */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 1)) /* DataPoint */
		goto out_buf;

	ok = (find_at_level(buf.data, pos, end, 4, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_FIXED64 && vl == 8);

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_metric_free(m);
	return ok;
}

/* Walk: → Metric → Histogram{9} → DataPoint → count{4}+sum{5}+buckets{6}+
 * bounds{7}; also agg_temp{2} at Histogram level. */
static int
prop_metrics_histogram_field_nums(uint64_t seed)
{
	double bounds[3] = { 1.0, 10.0, 100.0 };
	otlp_metric_t *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	int ok = 0;
	size_t pos, end, hpos, hend;
	int wt;
	size_t vp, vl;
	int saw_at = 0, saw_count = 0, saw_sum = 0;
	int saw_buckets = 0, saw_bounds = 0;

	(void) seed;
	m = otlp_metric_create(
		OTLP_METRIC_HISTOGRAM, "latency", "ns", "", bounds, 3);
	if (!m)
		return 0;
	otlp_metric_record(m, 0.5);
	otlp_metric_record(m, 5.0);
	otlp_metric_record(m, 50.0);
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1)) /* ResourceMetrics */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 2)) /* ScopeMetrics */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 2)) /* Metric */
		goto out_buf;
	hpos = pos;
	hend = end;
	if (!descend(buf.data, &hpos, &hend, 9)) /* Histogram */
		goto out_buf;

	if (find_at_level(buf.data, hpos, hend, 2, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_VARINT)
		saw_at = 1;

	pos = hpos;
	end = hend;
	if (!descend(buf.data, &pos, &end, 1)) /* DataPoint */
		goto out_buf;

	if (find_at_level(buf.data, pos, end, 4, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_FIXED64)
		saw_count = 1;
	if (find_at_level(buf.data, pos, end, 5, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_FIXED64)
		saw_sum = 1;
	if (find_at_level(buf.data, pos, end, 6, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_LEN)
		saw_buckets = 1;
	if (find_at_level(buf.data, pos, end, 7, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_LEN)
		saw_bounds = 1;

	ok = saw_at && saw_count && saw_sum && saw_buckets && saw_bounds;

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_metric_free(m);
	return ok;
}

/* Walk: → Metric → Sum{5} → DataPoint → as_double{4}, verify value. */
static int
prop_metrics_counter_value(uint64_t seed)
{
	struct prng p;
	otlp_metric_t *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	double expected;
	int ok = 0;
	size_t pos, end;
	int wt;
	size_t vp, vl;

	prng_seed(&p, seed);
	expected = (double) (prng_next(&p) % 1000000);
	m = otlp_metric_create(OTLP_METRIC_COUNTER, "c", "", "", NULL, 0);
	if (!m)
		return 0;
	otlp_metric_record(m, expected);
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1) || /* ResourceMetrics */
		!descend(buf.data, &pos, &end, 2) || /* ScopeMetrics */
		!descend(buf.data, &pos, &end, 2) || /* Metric */
		!descend(buf.data, &pos, &end, 7) || /* Sum */
		!descend(buf.data, &pos, &end, 1)) /* DataPoint */
		goto out_buf;

	if (find_at_level(buf.data, pos, end, 4, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_FIXED64 && vl == 8)
	{
		uint64_t bits;
		double got;

		memcpy(&bits, buf.data + vp, 8);
		memcpy(&got, &bits, 8);
		ok = (got == expected);
	}

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_metric_free(m);
	return ok;
}

/* Walk: → Metric → Sum → DataPoint → attributes{7}[] → KeyValue →
 * AnyValue → the oneof member for the cycled type, verify value. */
static int
prop_metrics_attributes_roundtrip(uint64_t seed)
{
	struct prng p;
	otlp_metric_t *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	union
	{
		uint64_t u;
		double d;
	} v;
	uint8_t bytes[5];
	unsigned type;
	uint32_t fi;
	int exp_wt;
	int ok = 0;
	size_t pos, end;
	int wt;
	size_t vp, vl;
	size_t i;

	prng_seed(&p, seed);
	v.u = prng_next(&p);
	for (i = 0; i < sizeof(bytes); i++)
		bytes[i] = (uint8_t) prng_next(&p);
	m = otlp_metric_create(OTLP_METRIC_COUNTER, "c", "", "", NULL, 0);
	if (!m)
		return 0;
	otlp_metric_record(m, 1.0);
	/* AnyValue oneof: bool{2} VARINT, int64{3} VARINT,
	 * double{4} FIXED64, bytes{7} LEN. */
	type = (unsigned) (seed % 4);
	switch (type)
	{
		case 0:
			otlp_metric_set_attribute_int(
				m, "answer", (int64_t) v.u);
			fi = 3;
			exp_wt = OTLP_PB_WIRE_VARINT;
			break;
		case 1:
			otlp_metric_set_attribute_double(m, "answer", v.d);
			fi = 4;
			exp_wt = OTLP_PB_WIRE_FIXED64;
			break;
		case 2:
			otlp_metric_set_attribute_bool(m, "answer", v.u & 1);
			fi = 2;
			exp_wt = OTLP_PB_WIRE_VARINT;
			break;
		default:
			otlp_metric_set_attribute_bytes(
				m, "answer", bytes, sizeof(bytes));
			fi = 7;
			exp_wt = OTLP_PB_WIRE_LEN;
			break;
	}
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, "svc", NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1) || /* ResourceMetrics */
		!descend(buf.data, &pos, &end, 2) || /* ScopeMetrics */
		!descend(buf.data, &pos, &end, 2) || /* Metric */
		!descend(buf.data, &pos, &end, 7) || /* Sum */
		!descend(buf.data, &pos, &end, 1)) /* DataPoint */
		goto out_buf;

	/* DataPoint: attributes at field 7, LEN sub-message (KeyValue).
	 * Field 1 is reserved in opentelemetry-proto. */
	if (find_at_level(buf.data, pos, end, 7, &wt, &vp, &vl) &&
		wt == OTLP_PB_WIRE_LEN)
	{
		/* Inside KeyValue: key{1}, value{2}. Descend into value. */
		size_t kp = vp, ke = vp + vl;

		if (find_at_level(buf.data, kp, ke, 2, &wt, &vp, &vl) &&
			wt == OTLP_PB_WIRE_LEN)
		{
			/* AnyValue: the oneof member for `type`. */
			size_t ap = vp, ae = vp + vl;

			if (find_at_level(
				    buf.data, ap, ae, fi, &wt, &vp, &vl) &&
				wt == exp_wt)
			{
				if (type == 0 || type == 2)
				{
					size_t p2 = vp;
					uint64_t got;

					if (decode_varint(
						    buf.data, ae, &p2, &got) ==
							OTLP_OK &&
						got ==
							(type == 0 ? v.u
								   : (v.u & 1)))
						ok = 1;
				}
				else if (type == 1)
				{
					uint64_t got = 0;

					if (vl == sizeof(got))
					{
						for (i = 0; i < sizeof(got);
							i++)
							got |= (uint64_t) buf.data
									[vp + i]
								<< (8 * i);
						if (got == v.u)
							ok = 1;
					}
				}
				else
				{
					if (vl == sizeof(bytes) &&
						memcmp(buf.data + vp,
							bytes,
							sizeof(bytes)) == 0)
						ok = 1;
				}
			}
		}
	}

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_metric_free(m);
	return ok;
}

/* Verify DELTA aggregation temporality appears on the wire (field 2
 * inside Sum = VARINT with value 1). Default is CUMULATIVE (2). */
static int
prop_metrics_delta_temporality(uint64_t seed)
{
	otlp_metric_t *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	int ok = 0;
	size_t pos, end;
	int wt;
	size_t vp, vl;

	(void) seed;
	m = otlp_metric_create(OTLP_METRIC_COUNTER, "req", "1", "", NULL, 0);
	if (!m)
		return 0;
	otlp_metric_record(m, 1.0);
	if (otlp_metric_set_aggregation_temporality(m, OTLP_AGG_TEMP_DELTA) !=
		OTLP_OK)
		goto out;
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, "svc", NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1)) /* ResourceMetrics */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 2)) /* ScopeMetrics */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 2)) /* Metric */
		goto out_buf;
	if (!find_at_level(buf.data, pos, end, 7, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_LEN)
		goto out_buf;
	/* Inside Sum: find agg_temp (field 2) and decode its varint. */
	{
		size_t sp = vp, se = vp + vl;

		if (!find_at_level(buf.data, sp, se, 2, &wt, &vp, &vl) ||
			wt != OTLP_PB_WIRE_VARINT)
			goto out_buf;
		{
			uint64_t v = 0;
			size_t p2 = vp;
			if (decode_varint(buf.data, vp + vl, &p2, &v) !=
				OTLP_OK)
				goto out_buf;
			ok = (v == OTLP_AGG_TEMP_DELTA);
		}
	}

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_metric_free(m);
	return ok;
}

/* Verify is_monotonic=false appears on the wire as field 3 ABSENT
 * (proto3 omits false/default bool values; the collector interprets
 * absence as false). Default is true → field 3 present with value 1. */
static int
prop_metrics_non_monotonic_counter(uint64_t seed)
{
	otlp_metric_t *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	int ok = 0;
	size_t pos, end;
	int wt;
	size_t vp, vl;

	(void) seed;
	m = otlp_metric_create(OTLP_METRIC_COUNTER,
		"depth",
		"1",
		"queue depth (up/down)",
		NULL,
		0);
	if (!m)
		return 0;
	otlp_metric_record(m, 42.0);
	if (otlp_metric_set_monotonic(m, false) != OTLP_OK)
		goto out;
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, "svc", NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1))
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 2))
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 2))
		goto out_buf;
	if (!find_at_level(buf.data, pos, end, 7, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_LEN)
		goto out_buf;
	{
		size_t sp = vp, se = vp + vl;

		/* is_monotonic=false → field 3 ABSENT (proto3 omits false).
		 * The collector interprets absence as false. */
		ok = !find_at_level(buf.data, sp, se, 3, &wt, &vp, &vl);
	}

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_metric_free(m);
	return ok;
}

/* ExpHistogram wire-format regression (v0.5.49). zero_count must
 * emit as FIXED64 (was VARINT); positive.bucket_counts entries must
 * be varint-packed (was fixed64-packed). Verifies the encoder
 * matches opentelemetry-proto's Buckets { sint32 offset = 1;
 * repeated uint64 bucket_counts = 2; }. */
static int
prop_metrics_exp_histogram_field_nums(uint64_t seed)
{
	otlp_metric_t *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	uint64_t pos_counts[3] = { 1, 3, 2 };
	int ok = 0;
	size_t pos, end, ppos, pend;
	int wt;
	size_t vp, vl;

	(void) seed;
	m = otlp_metric_create(
		OTLP_METRIC_EXP_HISTOGRAM, "eh", "", "", NULL, 0);
	if (!m)
		return 0;
	/* pos_offset = 5 (non-zero so the encoder emits it; proto3 omits
	 * zero-valued scalars by default). */
	if (otlp_metric_set_exp_histogram(
		    m, 20, 5, pos_counts, 3, 0, NULL, 0) != OTLP_OK)
		goto out;
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1)) /* ResourceMetrics */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 2)) /* ScopeMetrics */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 2)) /* Metric */
		goto out_buf;
	{
		/* Walk the ExponentialHistogram wrapper level (oneof
		 * field 10) before descending into the data point. The
		 * wrapper carries aggregation_temporality at field 2
		 * (per opentelemetry-proto). Pre-v0.5.61 the encoder
		 * emitted this correctly via HIST_F_AGG_TEMP but the
		 * schema had no ExpHistogram entry. */
		size_t eh_pos = pos;
		size_t eh_end = end;
		int saw_agg_temp = 0;
		int eh_wt = 0;
		size_t eh_vp = 0;
		size_t eh_vl = 0;

		if (!descend(buf.data, &eh_pos, &eh_end, 10))
			goto out_buf;
		if (find_at_level(buf.data,
			    eh_pos,
			    eh_end,
			    2,
			    &eh_wt,
			    &eh_vp,
			    &eh_vl) &&
			eh_wt == OTLP_PB_WIRE_VARINT)
			saw_agg_temp = 1;
		if (!saw_agg_temp)
			goto out_buf;
	}
	if (!descend(buf.data, &pos, &end, 10)) /* ExponentialHistogram */
		goto out_buf;
	if (!descend(buf.data, &pos, &end, 1)) /* DataPoint */
		goto out_buf;

	/* Positive Buckets sub-message at field 8. */
	if (!find_at_level(buf.data, pos, end, 8, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_LEN)
		goto out_buf;
	ppos = vp;
	pend = vp + vl;

	/* offset at field 1 (sint32 zigzag → VARINT). Verify zigzag(5)=10. */
	if (!find_at_level(buf.data, ppos, pend, 1, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_VARINT)
		goto out_buf;
	{
		size_t p = vp;
		uint64_t got;

		if (decode_varint(buf.data, vp + vl, &p, &got) != OTLP_OK)
			goto out_buf;
		/* zigzag32(5) = 10. */
		if (got != 10)
			goto out_buf;
	}

	/* bucket_counts at field 2 (packed repeated uint64 → LEN). */
	if (!find_at_level(buf.data, ppos, pend, 2, &wt, &vp, &vl) ||
		wt != OTLP_PB_WIRE_LEN)
		goto out_buf;
	{
		/* Decode 3 varints from the packed payload. */
		size_t p = vp;
		size_t pe = vp + vl;
		uint64_t got[3] = { 0 };
		int n_decoded = 0;

		while (p < pe && n_decoded < 3)
		{
			uint64_t v;

			if (decode_varint(buf.data, pe, &p, &v) != OTLP_OK)
				goto out_buf;
			got[n_decoded++] = v;
		}
		ok = (n_decoded == 3 && p == pe && got[0] == 1 && got[1] == 3 &&
			got[2] == 2);
	}

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_metric_free(m);
	return ok;
}

/* Regression (v0.5.62): integer overflow in metric size
 * calculations. A caller-supplied count that would cause
 * count * sizeof(...) to wrap must be rejected. */
static int
prop_metric_rejects_overflow_sizes(uint64_t seed)
{
	double dummy_bounds[1] = { 1.0 };
	uint64_t dummy_counts[1] = { 1 };
	otlp_metric_t *m;
	otlp_status_t st;

	(void) seed;

	/* Histogram with SIZE_MAX bounds → overflow in bounds
	 * allocation. Must return NULL. */
	m = otlp_metric_create(
		OTLP_METRIC_HISTOGRAM, "h", "", "", dummy_bounds, (size_t) -1);
	if (m)
	{
		otlp_metric_free(m);
		return 0;
	}

	/* ExpHistogram setter with SIZE_MAX pos counts → overflow.
	 * Must return INVALID_ARGUMENT. */
	m = otlp_metric_create(
		OTLP_METRIC_EXP_HISTOGRAM, "eh", "", "", NULL, 0);
	if (!m)
		return 0;
	st = otlp_metric_set_exp_histogram(
		m, 20, 0, dummy_counts, (size_t) -1, 0, NULL, 0);
	if (st != OTLP_ERR_INVALID_ARGUMENT)
	{
		otlp_metric_free(m);
		return 0;
	}
	otlp_metric_free(m);
	return 1;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(
		prop_metrics_empty_request, "prop_metrics_empty_request", 1, 1);
	failures += property_run(prop_metrics_counter_field_nums,
		"prop_metrics_counter_field_nums",
		1,
		1);
	failures += property_run(prop_metrics_gauge_field_nums,
		"prop_metrics_gauge_field_nums",
		1,
		1);
	failures += property_run(prop_metrics_histogram_field_nums,
		"prop_metrics_histogram_field_nums",
		1,
		1);
	failures += property_run(prop_metrics_counter_value,
		"prop_metrics_counter_value",
		200,
		1);
	failures += property_run(prop_metrics_attributes_roundtrip,
		"prop_metrics_attributes_roundtrip",
		200,
		1);
	failures += property_run(prop_metrics_delta_temporality,
		"prop_metrics_delta_temporality",
		5,
		1);
	failures += property_run(prop_metrics_non_monotonic_counter,
		"prop_metrics_non_monotonic_counter",
		5,
		1);
	failures += property_run(prop_metrics_exp_histogram_field_nums,
		"prop_metrics_exp_histogram_field_nums",
		5,
		1);
	failures += property_run(prop_metric_rejects_overflow_sizes,
		"prop_metric_rejects_overflow_sizes",
		1,
		1);

	if (failures)
		printf("[property] %d metrics property(ies) failed\n",
			failures);
	else
		printf("[property] all metrics properties passed\n");
	return failures ? 1 : 0;
}
