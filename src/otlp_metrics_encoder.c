/* SPDX-License-Identifier: Apache-2.0 */
/*
 * OTLP Metrics encoder. Produces ExportMetricsServiceRequest wire bytes.
 *
 * All field numbers come from src/otlp_schema.h (single source of
 * truth). The encoder is table-driven for the per-metric-type
 * dispatch (counter / gauge / histogram): adding a new metric type
 * (e.g., ExponentialHistogram) means adding one schema table entry,
 * one encoder function, and one dispatch-table slot — no switch to
 * modify. OCP.
 *
 * Reuses shared helpers from otlp_messages.c (otlp_emit_resource,
 * otlp_emit_instrumentation_scope, otlp_encode_key_value) so the
 * resource/scope envelope is identical across signals. DRY.
 */
#include "metric_internal.h"
#include "otlp_messages.h"
#include "otlp_schema.h"
#include "platform.h"
#include "protobuf_encode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Field-number accessors — single source of truth is otlp_schema.h. */
#define EMSR_F_RESOURCE_METRICS OTLP_EMSR_FIELDS[OTLP_EMSR_FI_RESOURCE_METRICS].number
#define RM_F_RESOURCE	       OTLP_RM_FIELDS[OTLP_RM_FI_RESOURCE].number
#define RM_F_SCOPE_METRICS     OTLP_RM_FIELDS[OTLP_RM_FI_SCOPE_METRICS].number
#define SM_F_SCOPE	       OTLP_SM_FIELDS[OTLP_SM_FI_SCOPE].number
#define SM_F_METRICS	       OTLP_SM_FIELDS[OTLP_SM_FI_METRICS].number
#define METRIC_F_NAME	       OTLP_METRIC_FIELDS[OTLP_METRIC_FI_NAME].number
#define METRIC_F_DESCRIPTION    OTLP_METRIC_FIELDS[OTLP_METRIC_FI_DESCRIPTION].number
#define METRIC_F_UNIT	       OTLP_METRIC_FIELDS[OTLP_METRIC_FI_UNIT].number
#define METRIC_F_GAUGE	       OTLP_METRIC_FIELDS[OTLP_METRIC_FI_GAUGE].number
#define METRIC_F_SUM	       OTLP_METRIC_FIELDS[OTLP_METRIC_FI_SUM].number
#define METRIC_F_HISTOGRAM      OTLP_METRIC_FIELDS[OTLP_METRIC_FI_HISTOGRAM].number
#define SUM_F_DATA_POINTS       OTLP_SUM_FIELDS[OTLP_SUM_FI_DATA_POINTS].number
#define SUM_F_AGG_TEMP	       OTLP_SUM_FIELDS[OTLP_SUM_FI_AGG_TEMP].number
#define SUM_F_IS_MONOTONIC      OTLP_SUM_FIELDS[OTLP_SUM_FI_IS_MONOTONIC].number
#define GAUGE_F_DATA_POINTS     OTLP_GAUGE_FIELDS[OTLP_GAUGE_FI_DATA_POINTS].number
#define HIST_F_DATA_POINTS      OTLP_HIST_FIELDS[OTLP_HIST_FI_DATA_POINTS].number
#define HIST_F_AGG_TEMP	       OTLP_HIST_FIELDS[OTLP_HIST_FI_AGG_TEMP].number
#define NDP_F_ATTRIBUTES       OTLP_NDP_FIELDS[OTLP_NDP_FI_ATTRIBUTES].number
#define NDP_F_START_TIME       OTLP_NDP_FIELDS[OTLP_NDP_FI_START_TIME].number
#define NDP_F_TIME	       OTLP_NDP_FIELDS[OTLP_NDP_FI_TIME].number
#define NDP_F_AS_DOUBLE        OTLP_NDP_FIELDS[OTLP_NDP_FI_AS_DOUBLE].number
#define HDP_F_ATTRIBUTES       OTLP_HDP_FIELDS[OTLP_HDP_FI_ATTRIBUTES].number
#define HDP_F_START_TIME       OTLP_HDP_FIELDS[OTLP_HDP_FI_START_TIME].number
#define HDP_F_TIME	       OTLP_HDP_FIELDS[OTLP_HDP_FI_TIME].number
#define HDP_F_COUNT	       OTLP_HDP_FIELDS[OTLP_HDP_FI_COUNT].number
#define HDP_F_SUM	       OTLP_HDP_FIELDS[OTLP_HDP_FI_SUM].number
#define HDP_F_BUCKET_COUNTS    OTLP_HDP_FIELDS[OTLP_HDP_FI_BUCKET_COUNTS].number
#define HDP_F_EXPLICIT_BOUNDS  OTLP_HDP_FIELDS[OTLP_HDP_FI_EXPLICIT_BOUNDS].number
#define HDP_F_MIN	       OTLP_HDP_FIELDS[OTLP_HDP_FI_MIN].number
#define HDP_F_MAX	       OTLP_HDP_FIELDS[OTLP_HDP_FI_MAX].number

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

/* Append a packed repeated field (tag + len + payload). */
static otlp_status_t
emit_packed_field(struct otlp_pb_buf *parent, uint32_t field_num,
		  const uint8_t *payload, size_t payload_len)
{
	if (payload_len == 0)
		return OTLP_OK;
	return otlp_pb_field_bytes(parent, field_num, payload, payload_len);
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

	st = otlp_pb_field_fixed64(&sub, HDP_F_COUNT,
				   otlp_metric_get_count(m));
	if (st != OTLP_OK)
		goto out;

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

/* ── Per-metric-type dispatch (table-driven, OCP) ────────────────
 *
 * Each metric type registers:
 *   - the Metric oneof field number it emits as (e.g., Sum=7)
 *   - the encoder function for the data point body
 *
 * Adding a new metric type is a table entry, not a switch mod.
 */

typedef otlp_status_t (*metric_data_point_fn)(
    struct otlp_pb_buf *parent, uint32_t field_num, const otlp_metric_t *m);

struct metric_kind_spec {
	unsigned		  oneof_field_index;
	metric_data_point_fn  data_point_encode;
};

static const struct metric_kind_spec metric_kind_specs[] = {
	[OTLP_METRIC_COUNTER]   = { OTLP_METRIC_FI_SUM,       emit_number_data_point   },
	[OTLP_METRIC_GAUGE]	    = { OTLP_METRIC_FI_GAUGE,     emit_number_data_point   },
	[OTLP_METRIC_HISTOGRAM] = { OTLP_METRIC_FI_HISTOGRAM, emit_histogram_data_point},
};

/* Per-kind extra fields emitted inside the oneof wrapper, after the
 * data points. Counter requires agg_temp + is_monotonic; Histogram
 * requires agg_temp; Gauge has none. */
static otlp_status_t
emit_kind_extra_fields(struct otlp_pb_buf *wrapper,
		       otlp_metric_type_t kind)
{
	switch (kind) {
	case OTLP_METRIC_COUNTER:
		if (otlp_pb_field_varint(wrapper, SUM_F_AGG_TEMP,
					 OTLP_AGG_TEMP_CUMULATIVE) != OTLP_OK)
			return OTLP_ERR_NOMEM;
		return otlp_pb_field_varint(wrapper, SUM_F_IS_MONOTONIC, 1);
	case OTLP_METRIC_HISTOGRAM:
		return otlp_pb_field_varint(wrapper, HIST_F_AGG_TEMP,
					   OTLP_AGG_TEMP_CUMULATIVE);
	default:
		return OTLP_OK;
	}
}

static otlp_status_t
emit_metric(struct otlp_pb_buf *parent, uint32_t field_num,
	    const otlp_metric_t *m)
{
	struct otlp_pb_buf sub = { 0 };
	otlp_status_t	    st;
	otlp_metric_type_t  kind = otlp_metric_get_type(m);

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

	/* Table-driven dispatch on metric kind. */
	if ((unsigned)kind < sizeof(metric_kind_specs) / sizeof(metric_kind_specs[0]) &&
	    metric_kind_specs[kind].data_point_encode) {
		const struct metric_kind_spec *spec = &metric_kind_specs[kind];
		uint32_t oneof_field = OTLP_METRIC_FIELDS[spec->oneof_field_index].number;
		struct otlp_pb_buf wrapper = { 0 };

		st = otlp_pb_buf_init(&wrapper, 0);
		if (st != OTLP_OK)
			goto out;
		st = spec->data_point_encode(&wrapper, 1, m);
		if (st == OTLP_OK)
			st = emit_kind_extra_fields(&wrapper, kind);
		if (st == OTLP_OK)
			st = otlp_pb_field_message(&sub, oneof_field,
						   wrapper.data, wrapper.len);
		otlp_pb_buf_free(&wrapper);
		if (st != OTLP_OK)
			goto out;
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
