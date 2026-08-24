/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef OTLP_C_METRIC_INTERNAL_H
#define OTLP_C_METRIC_INTERNAL_H

#include <otlp-c/metric.h>
#include "span_internal.h"

#include <stddef.h>
#include <stdint.h>

/* Exemplar model (v0.8.0): one value (double or int64), optional
 * trace/span correlation, optional timestamp. */
struct otlp_exemplar
{
	double double_val;
	int64_t int_val;
	bool has_double;
	bool has_int;
	uint8_t trace_id[16];
	uint8_t span_id[8];
	bool has_trace;
	bool has_span;
	uint64_t time_unix_nano;
	bool has_time;
};

struct otlp_metric
{
	otlp_metric_type_t type;
	char *name;
	char *unit;
	char *description;
	uint64_t start_time;
	uint64_t time;
	bool has_start;
	bool has_time;
	/* Attributes (grow-on-demand otlp_attr_vec, v0.5.75). */
	struct otlp_attr_vec attrs;
	/* Aggregation temporality (OTLP_AGG_TEMP_DELTA or _CUMULATIVE).
	 * Default CUMULATIVE. Applies to Sum/Histogram/ExpHistogram;
	 * Gauge does not use it. */
	uint8_t agg_temp;
	/* is_monotonic for Sum (Counter). Default true. Meaningful only
	 * for OTLP_METRIC_COUNTER; ignored by other types. */
	bool is_monotonic;
	/* Counter / gauge: */
	double value;
	/* Histogram: */
	uint64_t count;
	double sum;
	double min;
	double max;
	bool has_minmax;
	double *bounds; /* owned, sorted */
	size_t n_bounds;
	uint64_t *bucket_counts; /* n_bounds+1 entries, owned */
	/* ExponentialHistogram: */
	int32_t exp_scale;
	uint64_t exp_zero_count;
	int32_t exp_pos_offset;
	uint64_t *exp_pos_counts; /* owned */
	size_t exp_pos_n;
	int32_t exp_neg_offset;
	uint64_t *exp_neg_counts; /* owned */
	size_t exp_neg_n;
	bool has_exp_scale;
	/* Exemplars (v0.8.0): owned clones added via
	 * otlp_metric_add_exemplar; emitted on the data point. */
	struct otlp_exemplar *exemplars;
	size_t n_exemplars;
	size_t cap_exemplars;
};

/* Accessors for encoder. */
/* Exemplar accessors for the encoder. */
size_t
otlp_metric_get_n_exemplars(const otlp_metric_t *m);
const struct otlp_exemplar *
otlp_metric_get_exemplar(const otlp_metric_t *m, size_t i);

const char *
otlp_metric_get_name(const otlp_metric_t *m);
const char *
otlp_metric_get_unit(const otlp_metric_t *m);
const char *
otlp_metric_get_description(const otlp_metric_t *m);
otlp_metric_type_t
otlp_metric_get_type(const otlp_metric_t *m);
uint64_t
otlp_metric_get_start_time(const otlp_metric_t *m);
uint64_t
otlp_metric_get_time(const otlp_metric_t *m);
bool
otlp_metric_has_start(const otlp_metric_t *m);
bool
otlp_metric_has_time(const otlp_metric_t *m);
double
otlp_metric_get_value(const otlp_metric_t *m);
uint64_t
otlp_metric_get_count(const otlp_metric_t *m);
double
otlp_metric_get_sum(const otlp_metric_t *m);
double
otlp_metric_get_min(const otlp_metric_t *m);
double
otlp_metric_get_max(const otlp_metric_t *m);
bool
otlp_metric_has_minmax(const otlp_metric_t *m);
const double *
otlp_metric_get_bounds(const otlp_metric_t *m, size_t *n);
const uint64_t *
otlp_metric_get_buckets(const otlp_metric_t *m);
int32_t
otlp_metric_get_exp_scale(const otlp_metric_t *m);
uint64_t
otlp_metric_get_exp_zero_count(const otlp_metric_t *m);
int32_t
otlp_metric_get_exp_pos_offset(const otlp_metric_t *m);
const uint64_t *
otlp_metric_get_exp_pos_counts(const otlp_metric_t *m, size_t *n);
int32_t
otlp_metric_get_exp_neg_offset(const otlp_metric_t *m);
const uint64_t *
otlp_metric_get_exp_neg_counts(const otlp_metric_t *m, size_t *n);
bool
otlp_metric_has_exp_scale(const otlp_metric_t *m);
const struct otlp_attribute *
otlp_metric_get_attrs(const otlp_metric_t *m, size_t *n);
uint8_t
otlp_metric_get_agg_temp(const otlp_metric_t *m);
bool
otlp_metric_get_is_monotonic(const otlp_metric_t *m);

/* Test-only: sizeof(struct otlp_metric). The struct stays opaque;
 * tests use this to regression-guard the size budget (see
 * TODO.complete/109 — the struct was 4312 bytes with an inline
 * attrs[128] array). */
size_t
otlp_metric_struct_size(void);

/* Deep-copy a metric. Returns NULL on OOM. The caller owns the
 * result; free with otlp_metric_free. */
otlp_metric_t *
otlp_metric_clone(const otlp_metric_t *src);

#endif
