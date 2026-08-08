/* SPDX-License-Identifier: Apache-2.0 */
/*
 * OTLP Metrics — public API for counter, gauge, and histogram types.
 *
 * Three metric types are supported in v0.4:
 *   - Counter (OTLP Sum with is_monotonic=true, cumulative temporality)
 *   - Gauge (OTLP Gauge with a single data point)
 *   - Histogram (OTLP Histogram with explicit bucket boundaries)
 *
 * ExponentialHistogram and Summary are deferred to a future version.
 *
 * Lifetime: caller-owned. Construct via otlp_metric_create(); free
 * via otlp_metric_free().
 *
 * Thread-safety: single-threaded, same as spans. The caller builds
 * a metric on one thread, then passes it to the encoder/exporter.
 */
#ifndef OTLP_C_METRIC_H
#define OTLP_C_METRIC_H

#include <otlp-c/status.h>
#include <otlp-c/visibility.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct otlp_metric otlp_metric_t;

typedef enum {
	OTLP_METRIC_COUNTER	       = 1,
	OTLP_METRIC_GAUGE	       = 2,
	OTLP_METRIC_HISTOGRAM	       = 3,
	OTLP_METRIC_EXP_HISTOGRAM      = 4,
} otlp_metric_type_t;

/* Aggregation temporality (OTLP enum values). */
#define OTLP_AGG_TEMP_UNSPECIFIED  0
#define OTLP_AGG_TEMP_DELTA	      1
#define OTLP_AGG_TEMP_CUMULATIVE   2

/* Construct a metric. `unit` and `description` may be NULL.
 *
 * For HISTOGRAM: pass the explicit bucket boundaries (sorted ascending).
 * The library copies the array. Pass NULL + 0 for no explicit bounds
 * (the histogram becomes a simple sum/count/min/max without buckets). */
OTLP_C_EXPORT
otlp_metric_t *
otlp_metric_create(otlp_metric_type_t type,
		   const char	   *name,
		   const char	   *unit,
		   const char	   *description,
		   const double	   *histogram_bounds,
		   size_t	    histogram_n_bounds);

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
otlp_status_t otlp_metric_set_time(otlp_metric_t *m, uint64_t unix_nano);

OTLP_C_EXPORT
otlp_status_t otlp_metric_mark_time(otlp_metric_t *m); /* time = now */

/* Set an attribute on the current data point (same semantics as
 * otlp_span_set_attribute_*). Fixed cap: 128 attributes. */
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

#ifdef __cplusplus
}
#endif

#endif
