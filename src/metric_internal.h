/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OTLP_C_METRIC_INTERNAL_H
#define OTLP_C_METRIC_INTERNAL_H

#include <otlp-c/metric.h>
#include "span_internal.h"

#include <stddef.h>
#include <stdint.h>

struct otlp_metric {
	otlp_metric_type_t  type;
	char		   *name;
	char		   *unit;
	char		   *description;
	uint64_t	    start_time;
	uint64_t	    time;
	bool		    has_start;
	bool		    has_time;
	struct otlp_attribute attrs[128];
	size_t		    n_attrs;
	/* Aggregation temporality (OTLP_AGG_TEMP_DELTA or _CUMULATIVE).
	 * Default CUMULATIVE. Applies to Sum/Histogram/ExpHistogram;
	 * Gauge does not use it. */
	uint8_t		    agg_temp;
	/* is_monotonic for Sum (Counter). Default true. Meaningful only
	 * for OTLP_METRIC_COUNTER; ignored by other types. */
	bool		    is_monotonic;
	/* Counter / gauge: */
	double		    value;
	/* Histogram: */
	uint64_t	    count;
	double		    sum;
	double		    min;
	double		    max;
	bool		    has_minmax;
	double		   *bounds;	    /* owned, sorted */
	size_t		    n_bounds;
	uint64_t	   *bucket_counts;   /* n_bounds+1 entries, owned */
	/* ExponentialHistogram: */
	int32_t		    exp_scale;
	uint64_t	    exp_zero_count;
	int32_t		    exp_pos_offset;
	uint64_t	   *exp_pos_counts;   /* owned */
	size_t		    exp_pos_n;
	int32_t		    exp_neg_offset;
	uint64_t	   *exp_neg_counts;   /* owned */
	size_t		    exp_neg_n;
	bool		    has_exp_scale;
};

/* Accessors for encoder. */
const char	       *otlp_metric_get_name(const otlp_metric_t *m);
const char	       *otlp_metric_get_unit(const otlp_metric_t *m);
const char	       *otlp_metric_get_description(const otlp_metric_t *m);
otlp_metric_type_t	otlp_metric_get_type(const otlp_metric_t *m);
uint64_t		otlp_metric_get_start_time(const otlp_metric_t *m);
uint64_t		otlp_metric_get_time(const otlp_metric_t *m);
bool			otlp_metric_has_start(const otlp_metric_t *m);
bool			otlp_metric_has_time(const otlp_metric_t *m);
double			otlp_metric_get_value(const otlp_metric_t *m);
uint64_t		otlp_metric_get_count(const otlp_metric_t *m);
double			otlp_metric_get_sum(const otlp_metric_t *m);
double			otlp_metric_get_min(const otlp_metric_t *m);
double			otlp_metric_get_max(const otlp_metric_t *m);
bool			otlp_metric_has_minmax(const otlp_metric_t *m);
const double	       *otlp_metric_get_bounds(const otlp_metric_t *m, size_t *n);
const uint64_t	       *otlp_metric_get_buckets(const otlp_metric_t *m);
int32_t			otlp_metric_get_exp_scale(const otlp_metric_t *m);
uint64_t		otlp_metric_get_exp_zero_count(const otlp_metric_t *m);
int32_t			otlp_metric_get_exp_pos_offset(const otlp_metric_t *m);
const uint64_t	       *otlp_metric_get_exp_pos_counts(const otlp_metric_t *m, size_t *n);
int32_t			otlp_metric_get_exp_neg_offset(const otlp_metric_t *m);
const uint64_t	       *otlp_metric_get_exp_neg_counts(const otlp_metric_t *m, size_t *n);
bool			otlp_metric_has_exp_scale(const otlp_metric_t *m);
const struct otlp_attribute *otlp_metric_get_attrs(const otlp_metric_t *m, size_t *n);
uint8_t			otlp_metric_get_agg_temp(const otlp_metric_t *m);
bool			otlp_metric_get_is_monotonic(const otlp_metric_t *m);

#endif
