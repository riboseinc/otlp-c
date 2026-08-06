/* SPDX-License-Identifier: Apache-2.0 */
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
 *   prop_metrics_attributes_roundtrip — metric attribute as KeyValue int.
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
find_at_level(const uint8_t *data, size_t pos, size_t end, uint32_t fnum,
	      int *wt_out, size_t *val_pos, size_t *val_len)
{
	while (pos < end) {
		uint32_t fn = 0;
		int	    wt = 0;
		size_t	   vstart;
		size_t	   vlen = 0;
		otlp_status_t st = decode_tag(data, end, &pos, &fn, &wt);

		if (st != OTLP_OK)
			return 0;
		vstart = pos;
		if (wt == OTLP_PB_WIRE_VARINT) {
			uint64_t v;

			if (decode_varint(data, end, &pos, &v) != OTLP_OK)
				return 0;
			vlen = pos - vstart;
		} else if (wt == OTLP_PB_WIRE_FIXED64) {
			if (pos + 8 > end)
				return 0;
			pos += 8;
			vlen = 8;
		} else if (wt == OTLP_PB_WIRE_FIXED32) {
			if (pos + 4 > end)
				return 0;
			pos += 4;
			vlen = 4;
		} else if (wt == OTLP_PB_WIRE_LEN) {
			uint64_t l;

			if (decode_varint(data, end, &pos, &l) != OTLP_OK)
				return 0;
			vlen = (size_t) l;
			vstart = pos;
			if (pos + vlen > end)
				return 0;
			pos += vlen;
		} else {
			return 0;
		}
		if (fn == fnum) {
			if (wt_out && val_pos && val_len) {
				*wt_out  = wt;
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
	int    wt = 0;
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
	int		       ok = 0;

	(void) seed;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	if (otlp_encode_export_metrics_service_request(
		    &buf, NULL, NULL, NULL, NULL, 0) == OTLP_OK)
		ok = (buf.len == 0);
	otlp_pb_buf_free(&buf);
	return ok;
}

/* Walk: EMSR{1} → RM → SM{2} → Metric{2} → name{1} + Sum{5}. */
static int
prop_metrics_counter_field_nums(uint64_t seed)
{
	otlp_metric_t     *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	int		       ok = 0;
	size_t	       pos, end;
	int		       wt;
	size_t	       vp, vl;
	int		       saw_name = 0, saw_sum = 0, saw_dp = 0;
	int		       saw_at = 0, saw_im = 0;

	(void) seed;
	m = otlp_metric_create(OTLP_METRIC_COUNTER, "requests", "1", "count", NULL, 0);
	if (!m)
		return 0;
	otlp_metric_record(m, 5.0);
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, "svc", "scope", "1.0", arr, 1) != OTLP_OK)
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
	    wt == OTLP_PB_WIRE_LEN) {
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

/* Walk: EMSR{1} → RM → SM{2} → Metric{2} → Gauge{5} → DataPoint → as_double{4}. */
static int
prop_metrics_gauge_field_nums(uint64_t seed)
{
	otlp_metric_t     *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	int		       ok = 0;
	size_t	       pos, end;
	int		       wt;
	size_t	       vp, vl;

	(void) seed;
	m = otlp_metric_create(OTLP_METRIC_GAUGE, "temperature", "C", "", NULL, 0);
	if (!m)
		return 0;
	otlp_metric_record(m, 42.5);
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, NULL, NULL, NULL, arr, 1) != OTLP_OK)
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
	double		     bounds[3] = {1.0, 10.0, 100.0};
	otlp_metric_t     *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	int		       ok = 0;
	size_t	       pos, end, hpos, hend;
	int		       wt;
	size_t	       vp, vl;
	int		       saw_at = 0, saw_count = 0, saw_sum = 0;
	int		       saw_buckets = 0, saw_bounds = 0;

	(void) seed;
	m = otlp_metric_create(OTLP_METRIC_HISTOGRAM, "latency", "ns", "",
			       bounds, 3);
	if (!m)
		return 0;
	otlp_metric_record(m, 0.5);
	otlp_metric_record(m, 5.0);
	otlp_metric_record(m, 50.0);
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, NULL, NULL, NULL, arr, 1) != OTLP_OK)
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
	struct prng	      p;
	otlp_metric_t     *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	double		      expected;
	int		      ok = 0;
	size_t		      pos, end;
	int		      wt;
	size_t		      vp, vl;

	prng_seed(&p, seed);
	expected = (double)(prng_next(&p) % 1000000);
	m = otlp_metric_create(OTLP_METRIC_COUNTER, "c", "", "", NULL, 0);
	if (!m)
		return 0;
	otlp_metric_record(m, expected);
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, NULL, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1) ||	/* ResourceMetrics */
	    !descend(buf.data, &pos, &end, 2) ||	/* ScopeMetrics */
	    !descend(buf.data, &pos, &end, 2) ||	/* Metric */
	    !descend(buf.data, &pos, &end, 7) ||	/* Sum */
	    !descend(buf.data, &pos, &end, 1))		/* DataPoint */
		goto out_buf;

	if (find_at_level(buf.data, pos, end, 4, &wt, &vp, &vl) &&
	    wt == OTLP_PB_WIRE_FIXED64 && vl == 8) {
		uint64_t bits;
		double   got;

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

/* Walk: → Metric → Sum → DataPoint → attributes{1}[] → KeyValue →
 * AnyValue → int_value{3}, verify value. */
static int
prop_metrics_attributes_roundtrip(uint64_t seed)
{
	struct prng	      p;
	otlp_metric_t     *m;
	struct otlp_pb_buf buf = { 0 };
	const otlp_metric_t *arr[1] = { NULL };
	int64_t	      v;
	int		      ok = 0;
	size_t		      pos, end;
	int		      wt;
	size_t		      vp, vl;

	prng_seed(&p, seed);
	v = (int64_t) prng_next(&p);
	m = otlp_metric_create(OTLP_METRIC_COUNTER, "c", "", "", NULL, 0);
	if (!m)
		return 0;
	otlp_metric_record(m, 1.0);
	otlp_metric_set_attribute_int(m, "answer", v);
	arr[0] = m;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_metrics_service_request(
		    &buf, "svc", NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;

	pos = 0;
	end = buf.len;
	if (!descend(buf.data, &pos, &end, 1) ||	/* ResourceMetrics */
	    !descend(buf.data, &pos, &end, 2) ||	/* ScopeMetrics */
	    !descend(buf.data, &pos, &end, 2) ||	/* Metric */
	    !descend(buf.data, &pos, &end, 7) ||	/* Sum */
	    !descend(buf.data, &pos, &end, 1))		/* DataPoint */
		goto out_buf;

	/* DataPoint: first attribute is at field 1, LEN sub-message (KeyValue). */
	if (find_at_level(buf.data, pos, end, 1, &wt, &vp, &vl) &&
	    wt == OTLP_PB_WIRE_LEN) {
		/* Inside KeyValue: key{1}, value{2}. Descend into value. */
		size_t kp = vp, ke = vp + vl;

		if (find_at_level(buf.data, kp, ke, 2, &wt, &vp, &vl) &&
		    wt == OTLP_PB_WIRE_LEN) {
			/* AnyValue: int_value{3} VARINT. */
			size_t ap = vp, ae = vp + vl;

			if (find_at_level(buf.data, ap, ae, 3, &wt, &vp, &vl) &&
			    wt == OTLP_PB_WIRE_VARINT) {
				size_t  p2 = vp;
				uint64_t got;

				if (decode_varint(buf.data, ae, &p2, &got) == OTLP_OK &&
				    (int64_t) got == v)
					ok = 1;
			}
		}
	}

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_metric_free(m);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_metrics_empty_request,
				 "prop_metrics_empty_request", 1, 1);
	failures += property_run(prop_metrics_counter_field_nums,
				 "prop_metrics_counter_field_nums", 1, 1);
	failures += property_run(prop_metrics_gauge_field_nums,
				 "prop_metrics_gauge_field_nums", 1, 1);
	failures += property_run(prop_metrics_histogram_field_nums,
				 "prop_metrics_histogram_field_nums", 1, 1);
	failures += property_run(prop_metrics_counter_value,
				 "prop_metrics_counter_value", 200, 1);
	failures += property_run(prop_metrics_attributes_roundtrip,
				 "prop_metrics_attributes_roundtrip", 200, 1);

	if (failures)
		printf("[property] %d metrics property(ies) failed\n", failures);
	else
		printf("[property] all metrics properties passed\n");
	return failures ? 1 : 0;
}
