/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * OTLP Metrics — public API for counter, gauge, histogram, and
 * exponential histogram types.
 *
 * Four metric types are supported (v0.5.x):
 *   - Counter (OTLP Sum; is_monotonic and aggregation_temporality
 *     are configurable via _set_monotonic and
 *     _set_aggregation_temporality — defaults: true, CUMULATIVE)
 *   - Gauge (instantaneous value)
 *   - Histogram (explicit bucket boundaries)
 *   - ExponentialHistogram (compact bucket representation)
 *
 * Summary is not supported (the OpenTelemetry spec recommends
 * Histogram or ExponentialHistogram for new code; Summary is a
 * legacy type).
 *
 * Lifetime: caller-owned. Construct via otlp_metric_create(); free
 * via otlp_metric_free().
 *
 * Return codes (every otlp_status_t setter): OTLP_OK on success;
 * OTLP_ERR_NULL for a NULL metric or key; OTLP_ERR_NOMEM on
 * allocation failure; OTLP_ERR_OVERFLOW past the 128-distinct-key
 * attribute cap; OTLP_ERR_INVALID_ARGUMENT for wrong-type
 * operations (e.g. exp-histogram data on a counter) or count
 * overflows. Inputs are deep-copied where owned.
 *
 * Thread-safety: single-threaded, same as spans. The caller builds
 * a metric on one thread, then passes it to the encoder/exporter.
 */
#ifndef OTLP_C_METRIC_H
#define OTLP_C_METRIC_H

#include <otlp-c/status.h>
#include <otlp-c/value.h>
#include <otlp-c/visibility.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

	typedef struct otlp_metric otlp_metric_t;

	typedef enum
	{
		OTLP_METRIC_COUNTER = 1,
		OTLP_METRIC_GAUGE = 2,
		OTLP_METRIC_HISTOGRAM = 3,
		OTLP_METRIC_EXP_HISTOGRAM = 4,
	} otlp_metric_type_t;

/* Aggregation temporality (OTLP enum values). */
#define OTLP_AGG_TEMP_UNSPECIFIED 0
#define OTLP_AGG_TEMP_DELTA 1
#define OTLP_AGG_TEMP_CUMULATIVE 2

	/* Construct a metric. `unit` and `description` may be NULL.
	 *
	 * For HISTOGRAM: pass the explicit bucket boundaries (sorted
	 * ascending). The library copies the array. Pass NULL + 0 for no
	 * explicit bounds (the histogram becomes a simple sum/count/min/max
	 * without buckets). */
	OTLP_C_EXPORT
	otlp_metric_t *otlp_metric_create(otlp_metric_type_t type,
		const char *name,
		const char *unit,
		const char *description,
		const double *histogram_bounds,
		size_t histogram_n_bounds);

	OTLP_C_EXPORT
	void otlp_metric_free(otlp_metric_t *m);

	/* Record a value.
	 * - Counter: adds `value` to the running sum.
	 * - Gauge: replaces the current value.
	 * - Histogram: increments count, adds to sum, updates min/max,
	 *   and increments the appropriate bucket. */
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_record(otlp_metric_t *m, double value);

	/* Set the data-point timestamps. Default start_time=0, time=0.
	 * The encoder emits 0 for unset timestamps (the collector will
	 * fill them in). */
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_set_start_time(otlp_metric_t *m,
		uint64_t unix_nano);
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_set_time(otlp_metric_t *m,
		uint64_t unix_nano);

	OTLP_C_EXPORT
	otlp_status_t otlp_metric_mark_time(otlp_metric_t *m); /* time = now */

	/* Set an attribute on the current data point (same semantics as
	 * otlp_span_set_attribute_*: a map — last write wins, type may
	 * change; max 128 distinct keys).
	 * String keys and string values must be valid UTF-8 (the
	 * proto3 string contract); invalid input returns
	 * OTLP_ERR_UTF8. bytes values are exempt. */
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_set_attribute_string(otlp_metric_t *m,
		const char *key,
		const char *value);
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_set_attribute_int(otlp_metric_t *m,
		const char *key,
		int64_t value);
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_set_attribute_double(otlp_metric_t *m,
		const char *key,
		double value);
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_set_attribute_bool(otlp_metric_t *m,
		const char *key,
		bool value);
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_set_attribute_bytes(otlp_metric_t *m,
		const char *key,
		const uint8_t *bytes,
		size_t len);

	/* Set an ArrayValue / KeyValueList attribute (deep-copied;
	 * same upsert semantics as the scalar setters). */
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_set_attribute_array(otlp_metric_t *m,
		const char *key,
		const otlp_value_t *items,
		size_t n);
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_set_attribute_kvlist(otlp_metric_t *m,
		const char *key,
		const otlp_kv_t *entries,
		size_t n);

	/* Set ExponentialHistogram bucket data. Only valid for
	 * OTLP_METRIC_EXP_HISTOGRAM. The library copies the arrays.
	 * Pass NULL + 0 for pos_counts / neg_counts to omit that side.
	 *
	 * `scale` is the base-2 resolution (0 = each bucket doubles;
	 * 20 = ~0.1% resolution). The bucket at offset `i` covers
	 * [2^((offset+i-1)/2^scale), 2^((offset+i)/2^scale)). */
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_set_exp_histogram(otlp_metric_t *m,
		int32_t scale,
		int32_t pos_offset,
		const uint64_t *pos_counts,
		size_t pos_n,
		int32_t neg_offset,
		const uint64_t *neg_counts,
		size_t neg_n);

	/* Set the aggregation temporality. Applies to Counter (Sum),
	 * Histogram, and ExponentialHistogram; ignored by Gauge.
	 *
	 * Pass OTLP_AGG_TEMP_DELTA (1) for delta-style reporting (value
	 * since last export) or OTLP_AGG_TEMP_CUMULATIVE (2) for
	 * cumulative (value since process start). Default: CUMULATIVE.
	 *
	 * Returns OTLP_ERR_INVALID_ARGUMENT if `temp` is not DELTA or
	 * CUMULATIVE. */
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_set_aggregation_temporality(otlp_metric_t *m,
		uint8_t temp);

	/* Set is_monotonic for Counter (Sum). Only meaningful for
	 * OTLP_METRIC_COUNTER; ignored by other types. Default: true.
	 *
	 * Set false for an up/down counter (a Sum that can decrease —
	 * e.g., queue depth, active connections). The backend uses this
	 * flag to interpret the metric correctly. */
	OTLP_C_EXPORT
	otlp_status_t otlp_metric_set_monotonic(otlp_metric_t *m,
		bool monotonic);

#ifdef __cplusplus
}
#endif

/* ── Exemplars (v0.8.0) ───────────────────────────────────────── */

/* A trace-correlated exemplar data point (OTLP metrics Exemplar):
 * a value captured at a moment, optionally tied to the trace/span
 * that produced it. Added to counter/gauge/histogram metrics via
 * otlp_metric_add_exemplar (cloned; caller retains ownership). */
typedef struct otlp_exemplar otlp_exemplar_t;

/* Create/free/clone. Zero-value exemplar until a value is set. */
OTLP_C_EXPORT
otlp_exemplar_t *otlp_exemplar_create(void);
OTLP_C_EXPORT
void otlp_exemplar_free(otlp_exemplar_t *ex);
OTLP_C_EXPORT
otlp_exemplar_t *otlp_exemplar_clone(const otlp_exemplar_t *ex);

/* Set the exemplar's value — double (field 2) or int64 (field 3).
 * The last setter called wins. */
OTLP_C_EXPORT
otlp_status_t otlp_exemplar_set_double_value(otlp_exemplar_t *ex,
	double value);
OTLP_C_EXPORT
otlp_status_t otlp_exemplar_set_int_value(otlp_exemplar_t *ex,
	int64_t value);

/* Correlate with the trace that produced the value. IDs are
 * copied; all-zero IDs are rejected (v0.5.54 rule). Either
 * pointer may be NULL to leave that ID unset. */
OTLP_C_EXPORT
otlp_status_t otlp_exemplar_set_trace_context(otlp_exemplar_t *ex,
	const uint8_t *trace_id /* 16 bytes */,
	const uint8_t *span_id /* 8 bytes */);

/* Stamp the exemplar with the current time (field 6). */
OTLP_C_EXPORT
otlp_status_t otlp_exemplar_mark_time(otlp_exemplar_t *ex);

/* Stamp an explicit time (field 6) — for backfilling or
 * deterministic tests. */
OTLP_C_EXPORT
otlp_status_t otlp_exemplar_set_timestamp(otlp_exemplar_t *ex,
	uint64_t time_unix_nano);

/* Clone `ex` into the metric's exemplar list (emitted on the
 * metric's data point: NumberDataPoint field 5, HistogramDataPoint
 * field 8). Metrics without a value set at encode time return
 * OTLP_ERR_INVALID_ARGUMENT. */
OTLP_C_EXPORT
otlp_status_t otlp_metric_add_exemplar(otlp_metric_t *m,
	const otlp_exemplar_t *ex);

#endif
