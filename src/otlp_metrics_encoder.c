/* SPDX-License-Identifier: Apache-2.0 */
/*
 * OTLP Metrics encoder. Produces ExportMetricsServiceRequest wire bytes.
 *
 * Reuses shared helpers from otlp_messages.c (otlp_emit_resource,
 * otlp_emit_instrumentation_scope, otlp_encode_key_value) so the
 * resource/scope envelope is identical to traces. Only the per-signal
 * body (Metric / DataPoint) is metrics-specific.
 *
 * Schema (opentelemetry-proto):
 *   ExportMetricsServiceRequest { repeated ResourceMetrics resource_metrics = 1; }
 *   ResourceMetrics { Resource resource = 1; repeated ScopeMetrics scope_metrics = 2; }
 *   ScopeMetrics { InstrumentationScope scope = 1; repeated Metric metrics = 2; }
 *   Metric { string name = 1; string description = 2; string unit = 3;
 *            oneof data { Gauge gauge = 5; Sum sum = 7; Histogram histogram = 9; ... } }
 *   Sum    { repeated NumberDataPoint data_points = 1;
 *            AggregationTemporality aggregation_temporality = 2; bool is_monotonic = 3; }
 *   Gauge  { repeated NumberDataPoint data_points = 1; }
 *   Histogram { repeated HistogramDataPoint data_points = 1;
 *               AggregationTemporality aggregation_temporality = 2; }
 *   NumberDataPoint { repeated KeyValue attributes = 1;
 *                     fixed64 start_time_unix_nano = 2;
 *                     fixed64 time_unix_nano = 3;
 *                     oneof value { double as_double = 4; sfixed64 as_int = 6; ... } }
 *   HistogramDataPoint { repeated KeyValue attributes = 1;
 *                        fixed64 start_time_unix_nano = 2;
 *                        fixed64 time_unix_nano = 3;
 *                        fixed64 count = 4;
 *                        double sum = 5;
 *                        repeated fixed64 bucket_counts = 6;     // packed
 *                        repeated double explicit_bounds = 7;    // packed
 *                        ... min/max = 9/10; }
 */
#include "metric_internal.h"
#include "otlp_messages.h"
#include "platform.h"
#include "protobuf_encode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define EMSR_F_RESOURCE_METRICS 1
#define RM_F_RESOURCE		 1
#define RM_F_SCOPE_METRICS	 2
#define SM_F_SCOPE		 1
#define SM_F_METRICS		 2
#define METRIC_F_NAME		 1
#define METRIC_F_DESCRIPTION	 2
#define METRIC_F_UNIT		 3
#define METRIC_F_GAUGE		 5
#define METRIC_F_SUM		 7
#define METRIC_F_HISTOGRAM	 9
#define SUM_F_DATA_POINTS	 1
#define SUM_F_AGG_TEMP		 2
#define SUM_F_IS_MONOTONIC	 3
#define GAUGE_F_DATA_POINTS	 1
#define HIST_F_DATA_POINTS	 1
#define HIST_F_AGG_TEMP		 2
#define NDP_F_ATTRIBUTES	 1
#define NDP_F_START_TIME	 2
#define NDP_F_TIME		 3
#define NDP_F_AS_DOUBLE		 4
#define HDP_F_ATTRIBUTES	 1
#define HDP_F_START_TIME	 2
#define HDP_F_TIME		 3
#define HDP_F_COUNT		 4
#define HDP_F_SUM		 5
#define HDP_F_BUCKET_COUNTS	 6
#define HDP_F_EXPLICIT_BOUNDS	 7
#define HDP_F_MIN		 9
#define HDP_F_MAX	 10

/* Append a packed repeated field (tag + len + payload) to `parent`. */
static otlp_status_t
emit_packed_field(struct otlp_pb_buf *parent, uint32_t field_num,
		  const uint8_t *payload, size_t payload_len)
{
	if (payload_len == 0)
		return OTLP_OK;
	return otlp_pb_field_bytes(parent, field_num, payload, payload_len);
}

static otlp_status_t
emit_attributes(struct otlp_pb_buf *sub, uint32_t field_num,
		const struct otlp_attribute *attrs, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		struct otlp_pb_buf kv = { 0 };
		otlp_status_t   st;

		st = otlp_pb_buf_init(&kv, 0);
		if (st != OTLP_OK)
			return st;
		st = otlp_encode_key_value(&kv, attrs[i].key, &attrs[i]);
		if (st == OTLP_OK)
			st = otlp_pb_field_message(sub, field_num, kv.data, kv.len);
		otlp_pb_buf_free(&kv);
		if (st != OTLP_OK)
			return st;
	}
	return OTLP_OK;
}

static otlp_status_t
emit_number_data_point(struct otlp_pb_buf *parent, uint32_t field_num,
		       const otlp_metric_t *m)
{
	struct otlp_pb_buf sub = { 0 };
	otlp_status_t	    st;
	size_t		    n;
	const struct otlp_attribute *attrs = otlp_metric_get_attrs(m, &n);

	st = otlp_pb_buf_init(&sub, 0);
	if (st != OTLP_OK)
		return st;

	st = emit_attributes(&sub, NDP_F_ATTRIBUTES, attrs, n);
	if (st != OTLP_OK)
		goto out;

	if (otlp_metric_has_start(m)) {
		st = otlp_pb_field_fixed64(&sub, NDP_F_START_TIME,
					   otlp_metric_get_start_time(m));
		if (st != OTLP_OK)
			goto out;
	}
	if (otlp_metric_has_time(m)) {
		st = otlp_pb_field_fixed64(&sub, NDP_F_TIME,
					   otlp_metric_get_time(m));
		if (st != OTLP_OK)
			goto out;
	}
	{
		uint64_t bits;
		double   v = otlp_metric_get_value(m);

		memcpy(&bits, &v, sizeof(bits));
		st = otlp_pb_tag(&sub, NDP_F_AS_DOUBLE, OTLP_PB_WIRE_FIXED64);
		if (st == OTLP_OK)
			st = otlp_pb_fixed64(&sub, bits);
		if (st != OTLP_OK)
			goto out;
	}
	st = otlp_pb_field_message(parent, field_num, sub.data, sub.len);

out:
	otlp_pb_buf_free(&sub);
	return st;
}

static otlp_status_t
emit_histogram_data_point(struct otlp_pb_buf *parent, uint32_t field_num,
			  const otlp_metric_t *m)
{
	struct otlp_pb_buf sub = { 0 };
	otlp_status_t	    st;
	size_t		    n;
	const struct otlp_attribute *attrs = otlp_metric_get_attrs(m, &n);
	size_t		    n_bounds = 0;
	const double	   *bounds   = otlp_metric_get_bounds(m, &n_bounds);
	const uint64_t	  *buckets  = otlp_metric_get_buckets(m);

	st = otlp_pb_buf_init(&sub, 0);
	if (st != OTLP_OK)
		return st;

	st = emit_attributes(&sub, HDP_F_ATTRIBUTES, attrs, n);
	if (st != OTLP_OK)
		goto out;

	if (otlp_metric_has_start(m)) {
		st = otlp_pb_field_fixed64(&sub, HDP_F_START_TIME,
					   otlp_metric_get_start_time(m));
		if (st != OTLP_OK)
			goto out;
	}
	if (otlp_metric_has_time(m)) {
		st = otlp_pb_field_fixed64(&sub, HDP_F_TIME,
					   otlp_metric_get_time(m));
		if (st != OTLP_OK)
			goto out;
	}

	/* count (fixed64, always emit). */
	st = otlp_pb_field_fixed64(&sub, HDP_F_COUNT,
				   otlp_metric_get_count(m));
	if (st != OTLP_OK)
		goto out;

	/* sum (double, fixed64, always emit). */
	{
		uint64_t bits;
		double   v = otlp_metric_get_sum(m);

		memcpy(&bits, &v, sizeof(bits));
		st = otlp_pb_tag(&sub, HDP_F_SUM, OTLP_PB_WIRE_FIXED64);
		if (st == OTLP_OK)
			st = otlp_pb_fixed64(&sub, bits);
		if (st != OTLP_OK)
			goto out;
	}

	/* bucket_counts: packed repeated fixed64. n_bounds+1 entries. */
	if (buckets && n_bounds > 0) {
		struct otlp_pb_buf packed = { 0 };
		size_t	       i;
		size_t	       n_b = n_bounds + 1;

		st = otlp_pb_buf_init(&packed, 0);
		if (st != OTLP_OK)
			goto out;
		for (i = 0; i < n_b; i++) {
			st = otlp_pb_fixed64(&packed, buckets[i]);
			if (st != OTLP_OK) {
				otlp_pb_buf_free(&packed);
				goto out;
			}
		}
		st = emit_packed_field(&sub, HDP_F_BUCKET_COUNTS,
				       packed.data, packed.len);
		otlp_pb_buf_free(&packed);
		if (st != OTLP_OK)
			goto out;
	}

	/* explicit_bounds: packed repeated double. n_bounds entries. */
	if (bounds && n_bounds > 0) {
		struct otlp_pb_buf packed = { 0 };
		size_t	       i;

		st = otlp_pb_buf_init(&packed, 0);
		if (st != OTLP_OK)
			goto out;
		for (i = 0; i < n_bounds; i++) {
			uint64_t bits;

			memcpy(&bits, &bounds[i], sizeof(bits));
			st = otlp_pb_fixed64(&packed, bits);
			if (st != OTLP_OK) {
				otlp_pb_buf_free(&packed);
				goto out;
			}
		}
		st = emit_packed_field(&sub, HDP_F_EXPLICIT_BOUNDS,
				       packed.data, packed.len);
		otlp_pb_buf_free(&packed);
		if (st != OTLP_OK)
			goto out;
	}

	if (otlp_metric_has_minmax(m)) {
		uint64_t bits;
		double   vmin = otlp_metric_get_min(m);
		double   vmax = otlp_metric_get_max(m);

		memcpy(&bits, &vmin, sizeof(bits));
		st = otlp_pb_tag(&sub, HDP_F_MIN, OTLP_PB_WIRE_FIXED64);
		if (st == OTLP_OK)
			st = otlp_pb_fixed64(&sub, bits);
		if (st != OTLP_OK)
			goto out;
		memcpy(&bits, &vmax, sizeof(bits));
		st = otlp_pb_tag(&sub, HDP_F_MAX, OTLP_PB_WIRE_FIXED64);
		if (st == OTLP_OK)
			st = otlp_pb_fixed64(&sub, bits);
		if (st != OTLP_OK)
			goto out;
	}

	st = otlp_pb_field_message(parent, field_num, sub.data, sub.len);

out:
	otlp_pb_buf_free(&sub);
	return st;
}

static otlp_status_t
emit_metric(struct otlp_pb_buf *parent, uint32_t field_num,
	    const otlp_metric_t *m)
{
	struct otlp_pb_buf sub = { 0 };
	otlp_status_t	    st;

	st = otlp_pb_buf_init(&sub, 0);
	if (st != OTLP_OK)
		return st;

	/* name (always emit, may be empty). */
	st = otlp_pb_tag(&sub, METRIC_F_NAME, OTLP_PB_WIRE_LEN);
	if (st == OTLP_OK)
		st = otlp_pb_string(&sub, otlp_metric_get_name(m));
	if (st != OTLP_OK)
		goto out;
	if (otlp_metric_get_description(m)[0]) {
		st = otlp_pb_field_string(&sub, METRIC_F_DESCRIPTION,
					  otlp_metric_get_description(m));
		if (st != OTLP_OK)
			goto out;
	}
	if (otlp_metric_get_unit(m)[0]) {
		st = otlp_pb_field_string(&sub, METRIC_F_UNIT,
					  otlp_metric_get_unit(m));
		if (st != OTLP_OK)
			goto out;
	}

	switch (otlp_metric_get_type(m)) {
	case OTLP_METRIC_COUNTER: {
		struct otlp_pb_buf sum_msg = { 0 };

		st = otlp_pb_buf_init(&sum_msg, 0);
		if (st != OTLP_OK)
			goto out;
		st = emit_number_data_point(&sum_msg, SUM_F_DATA_POINTS, m);
		if (st == OTLP_OK)
			st = otlp_pb_field_varint(&sum_msg, SUM_F_AGG_TEMP,
						  OTLP_AGG_TEMP_CUMULATIVE);
		if (st == OTLP_OK)
			st = otlp_pb_field_varint(&sum_msg, SUM_F_IS_MONOTONIC, 1);
		if (st == OTLP_OK)
			st = otlp_pb_field_message(&sub, METRIC_F_SUM,
						   sum_msg.data, sum_msg.len);
		otlp_pb_buf_free(&sum_msg);
		if (st != OTLP_OK)
			goto out;
		break;
	}
	case OTLP_METRIC_GAUGE: {
		struct otlp_pb_buf gauge_msg = { 0 };

		st = otlp_pb_buf_init(&gauge_msg, 0);
		if (st != OTLP_OK)
			goto out;
		st = emit_number_data_point(&gauge_msg, GAUGE_F_DATA_POINTS, m);
		if (st == OTLP_OK)
			st = otlp_pb_field_message(&sub, METRIC_F_GAUGE,
						   gauge_msg.data, gauge_msg.len);
		otlp_pb_buf_free(&gauge_msg);
		if (st != OTLP_OK)
			goto out;
		break;
	}
	case OTLP_METRIC_HISTOGRAM: {
		struct otlp_pb_buf hist_msg = { 0 };

		st = otlp_pb_buf_init(&hist_msg, 0);
		if (st != OTLP_OK)
			goto out;
		st = emit_histogram_data_point(&hist_msg, HIST_F_DATA_POINTS, m);
		if (st == OTLP_OK)
			st = otlp_pb_field_varint(&hist_msg, HIST_F_AGG_TEMP,
						  OTLP_AGG_TEMP_CUMULATIVE);
		if (st == OTLP_OK)
			st = otlp_pb_field_message(&sub, METRIC_F_HISTOGRAM,
						   hist_msg.data, hist_msg.len);
		otlp_pb_buf_free(&hist_msg);
		if (st != OTLP_OK)
			goto out;
		break;
	}
	default:
		break;
	}

	st = otlp_pb_field_message(parent, field_num, sub.data, sub.len);

out:
	otlp_pb_buf_free(&sub);
	return st;
}

otlp_status_t
otlp_encode_export_metrics_service_request(struct otlp_pb_buf	*out,
					   const char		*service_name,
					   const char		*scope_name,
					   const char		*scope_version,
					   const otlp_metric_t *const *metrics,
					   size_t		 n_metrics)
{
	struct otlp_pb_buf rm = { 0 }, sm = { 0 };
	otlp_status_t	    st;
	size_t		    i;

	if (!out)
		return OTLP_ERR_NULL;
	if (n_metrics == 0 && !(service_name && service_name[0]))
		return OTLP_OK;

	st = otlp_pb_buf_init(&rm, 0);
	if (st != OTLP_OK)
		return st;
	st = otlp_pb_buf_init(&sm, 0);
	if (st != OTLP_OK)
		goto out_rm;

	st = otlp_emit_resource(&rm, RM_F_RESOURCE, service_name);
	if (st != OTLP_OK)
		goto out_sm;

	st = otlp_emit_instrumentation_scope(&sm, SM_F_SCOPE,
					     scope_name, scope_version);
	if (st != OTLP_OK)
		goto out_sm;

	for (i = 0; i < n_metrics; i++) {
		st = emit_metric(&sm, SM_F_METRICS, metrics[i]);
		if (st != OTLP_OK)
			goto out_sm;
	}

	if (sm.len > 0)
		st = otlp_pb_field_message(&rm, RM_F_SCOPE_METRICS,
					   sm.data, sm.len);
	if (st == OTLP_OK)
		st = otlp_pb_field_message(out, EMSR_F_RESOURCE_METRICS,
					   rm.data, rm.len);

out_sm:
	otlp_pb_buf_free(&sm);
out_rm:
	otlp_pb_buf_free(&rm);
	return st;
}
