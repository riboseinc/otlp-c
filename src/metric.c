/* SPDX-License-Identifier: Apache-2.0 */
/*
 * OTLP Metric lifecycle. See include/otlp-c/metric.h.
 */
#include <otlp-c/metric.h>

#include "internal_util.h"
#include "metric_internal.h"
#include "platform.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define OTLP_METRIC_MAX_ATTRS 128

static void
metric_release_attrs(struct otlp_metric *m)
{
	size_t i;

	for (i = 0; i < m->n_attrs; i++) {
		otlp_free(m->attrs[i].key);
		switch (m->attrs[i].type) {
		case OTLP_ATTR_STRING:
			otlp_free(m->attrs[i].v.string_val);
			break;
		case OTLP_ATTR_BYTES:
			otlp_free(m->attrs[i].v.bytes_val.data);
			break;
		default:
			break;
		}
	}
	m->n_attrs = 0;
}

static otlp_status_t
metric_reserve_attr(struct otlp_metric *m, const char *key,
		    struct otlp_attribute **out)
{
	if (!m || !key)
		return OTLP_ERR_NULL;
	if (m->n_attrs >= OTLP_METRIC_MAX_ATTRS)
		return OTLP_ERR_OVERFLOW;
	{
		char *kc = otlp_dup_str(key);

		if (!kc)
			return OTLP_ERR_NOMEM;
		m->attrs[m->n_attrs].key = kc;
		m->attrs[m->n_attrs].v.string_val = NULL;
		*out = &m->attrs[m->n_attrs];
		return OTLP_OK;
	}
}

otlp_metric_t *
otlp_metric_create(otlp_metric_type_t type, const char *name,
		   const char *unit, const char *description,
		   const double *histogram_bounds, size_t histogram_n_bounds)
{
	struct otlp_metric *m;

	if (type < OTLP_METRIC_COUNTER || type > OTLP_METRIC_EXP_HISTOGRAM)
		return NULL;
	m = otlp_calloc(1, sizeof(*m));
	if (!m)
		return NULL;
	m->type = type;
	m->name = otlp_dup_str(name ? name : "");
	m->unit = otlp_dup_str(unit ? unit : "");
	m->description = otlp_dup_str(description ? description : "");
	m->agg_temp = OTLP_AGG_TEMP_CUMULATIVE;
	m->is_monotonic = true;
	if (!m->name || !m->unit || !m->description)
		goto fail;
	if (type == OTLP_METRIC_HISTOGRAM && histogram_bounds && histogram_n_bounds > 0) {
		/* Overflow check: histogram_n_bounds * sizeof(double) and
		 * (histogram_n_bounds + 1) * sizeof(uint64_t) must not
		 * wrap. Without this, a huge count produces an undersized
		 * allocation and the subsequent memcpy reads/writes OOB. */
		if (histogram_n_bounds > SIZE_MAX / sizeof(double))
			goto fail;
		if (histogram_n_bounds >= SIZE_MAX / sizeof(uint64_t))
			goto fail;
		m->bounds = otlp_malloc(histogram_n_bounds * sizeof(double));
		m->bucket_counts = otlp_calloc(histogram_n_bounds + 1, sizeof(uint64_t));
		if (!m->bounds || !m->bucket_counts)
			goto fail;
		memcpy(m->bounds, histogram_bounds,
		       histogram_n_bounds * sizeof(double));
		m->n_bounds = histogram_n_bounds;
	}
	return m;
fail:
	otlp_free(m->name);
	otlp_free(m->unit);
	otlp_free(m->description);
	otlp_free(m->bounds);
	otlp_free(m->bucket_counts);
	otlp_free(m);
	return NULL;
}

void
otlp_metric_free(otlp_metric_t *m)
{
	if (!m)
		return;
	otlp_free(m->name);
	otlp_free(m->unit);
	otlp_free(m->description);
	otlp_free(m->bounds);
	otlp_free(m->bucket_counts);
	otlp_free(m->exp_pos_counts);
	otlp_free(m->exp_neg_counts);
	metric_release_attrs(m);
	otlp_free(m);
}

otlp_status_t
otlp_metric_record(otlp_metric_t *m, double value)
{
	if (!m)
		return OTLP_ERR_NULL;
	switch (m->type) {
	case OTLP_METRIC_COUNTER:
		m->value += value;
		break;
	case OTLP_METRIC_GAUGE:
		m->value = value;
		break;
	case OTLP_METRIC_HISTOGRAM: {
		size_t i;

		m->count++;
		m->sum += value;
		if (!m->has_minmax || value < m->min)
			m->min = value;
		if (!m->has_minmax || value > m->max)
			m->max = value;
		m->has_minmax = true;
		if (m->bounds && m->bucket_counts) {
			for (i = 0; i < m->n_bounds; i++)
				if (value <= m->bounds[i])
					break;
			m->bucket_counts[i]++;
		} else {
			/* No explicit bounds — single bucket. */
			if (!m->bucket_counts) {
				m->bucket_counts = otlp_calloc(1, sizeof(uint64_t));
				if (!m->bucket_counts)
					return OTLP_ERR_NOMEM;
				m->n_bounds = 0;
			}
			m->bucket_counts[0]++;
		}
		break;
	}
	case OTLP_METRIC_EXP_HISTOGRAM:
		m->count++;
		m->sum += value;
		if (value == 0.0)
			m->exp_zero_count++;
		break;
	default:
		return OTLP_ERR_INVALID_ARGUMENT;
	}
	return OTLP_OK;
}

otlp_status_t
otlp_metric_set_start_time(otlp_metric_t *m, uint64_t unix_nano)
{
	if (!m)
		return OTLP_ERR_NULL;
	m->start_time = unix_nano;
	m->has_start = true;
	return OTLP_OK;
}

otlp_status_t
otlp_metric_set_time(otlp_metric_t *m, uint64_t unix_nano)
{
	if (!m)
		return OTLP_ERR_NULL;
	m->time = unix_nano;
	m->has_time = true;
	return OTLP_OK;
}

otlp_status_t
otlp_metric_mark_time(otlp_metric_t *m)
{
	if (!m)
		return OTLP_ERR_NULL;
	{
		uint64_t now;

		if (otlp_platform_now_unix_nano(&now) != OTLP_OK)
			return OTLP_ERR_NETWORK;
		m->time = now;
		m->has_time = true;
		return OTLP_OK;
	}
}

otlp_status_t
otlp_metric_set_attribute_string(otlp_metric_t *m, const char *key, const char *val)
{
	struct otlp_attribute *a;
	otlp_status_t st = metric_reserve_attr(m, key, &a);

	if (st != OTLP_OK)
		return st;
	a->type = OTLP_ATTR_STRING;
	a->v.string_val = otlp_dup_str(val ? val : "");
	if (!a->v.string_val) {
		otlp_free(a->key);
		return OTLP_ERR_NOMEM;
	}
	m->n_attrs++;
	return OTLP_OK;
}

otlp_status_t
otlp_metric_set_attribute_int(otlp_metric_t *m, const char *key, int64_t val)
{
	struct otlp_attribute *a;
	otlp_status_t st = metric_reserve_attr(m, key, &a);

	if (st != OTLP_OK)
		return st;
	a->type = OTLP_ATTR_INT64;
	a->v.int64_val = val;
	m->n_attrs++;
	return OTLP_OK;
}

otlp_status_t
otlp_metric_set_attribute_double(otlp_metric_t *m, const char *key, double val)
{
	struct otlp_attribute *a;
	otlp_status_t st = metric_reserve_attr(m, key, &a);

	if (st != OTLP_OK)
		return st;
	a->type = OTLP_ATTR_DOUBLE;
	a->v.double_val = val;
	m->n_attrs++;
	return OTLP_OK;
}

otlp_status_t
otlp_metric_set_exp_histogram(otlp_metric_t *m,
			      int32_t scale,
			      int32_t pos_offset,
			      const uint64_t *pos_counts,
			      size_t pos_n,
			      int32_t neg_offset,
			      const uint64_t *neg_counts,
			      size_t neg_n)
{
	if (!m)
		return OTLP_ERR_NULL;
	if (m->type != OTLP_METRIC_EXP_HISTOGRAM)
		return OTLP_ERR_INVALID_ARGUMENT;
	m->exp_scale = scale;
	m->has_exp_scale = true;
	if (pos_n > 0 && pos_counts) {
		if (pos_n > SIZE_MAX / sizeof(uint64_t))
			return OTLP_ERR_INVALID_ARGUMENT;
		m->exp_pos_offset = pos_offset;
		otlp_free(m->exp_pos_counts);
		m->exp_pos_counts = otlp_malloc(pos_n * sizeof(uint64_t));
		if (!m->exp_pos_counts)
			return OTLP_ERR_NOMEM;
		memcpy(m->exp_pos_counts, pos_counts, pos_n * sizeof(uint64_t));
		m->exp_pos_n = pos_n;
	}
	if (neg_n > 0 && neg_counts) {
		if (neg_n > SIZE_MAX / sizeof(uint64_t))
			return OTLP_ERR_INVALID_ARGUMENT;
		m->exp_neg_offset = neg_offset;
		otlp_free(m->exp_neg_counts);
		m->exp_neg_counts = otlp_malloc(neg_n * sizeof(uint64_t));
		if (!m->exp_neg_counts)
			return OTLP_ERR_NOMEM;
		memcpy(m->exp_neg_counts, neg_counts, neg_n * sizeof(uint64_t));
		m->exp_neg_n = neg_n;
	}
	return OTLP_OK;
}

/* ── Internal accessors ───────────────────────────────────────── */

const char *otlp_metric_get_name(const otlp_metric_t *m) { return m ? m->name : NULL; }
const char *otlp_metric_get_unit(const otlp_metric_t *m) { return m ? m->unit : NULL; }
const char *otlp_metric_get_description(const otlp_metric_t *m) { return m ? m->description : NULL; }
otlp_metric_type_t otlp_metric_get_type(const otlp_metric_t *m) { return m ? m->type : 0; }
uint64_t otlp_metric_get_start_time(const otlp_metric_t *m) { return m ? m->start_time : 0; }
uint64_t otlp_metric_get_time(const otlp_metric_t *m) { return m ? m->time : 0; }
bool otlp_metric_has_start(const otlp_metric_t *m) { return m ? m->has_start : false; }
bool otlp_metric_has_time(const otlp_metric_t *m) { return m ? m->has_time : false; }
double otlp_metric_get_value(const otlp_metric_t *m) { return m ? m->value : 0; }
uint64_t otlp_metric_get_count(const otlp_metric_t *m) { return m ? m->count : 0; }
double otlp_metric_get_sum(const otlp_metric_t *m) { return m ? m->sum : 0; }
double otlp_metric_get_min(const otlp_metric_t *m) { return m ? m->min : 0; }
double otlp_metric_get_max(const otlp_metric_t *m) { return m ? m->max : 0; }
bool otlp_metric_has_minmax(const otlp_metric_t *m) { return m ? m->has_minmax : false; }

const double *
otlp_metric_get_bounds(const otlp_metric_t *m, size_t *n)
{
	if (n)
		*n = m ? m->n_bounds : 0;
	return m ? m->bounds : NULL;
}

const uint64_t *
otlp_metric_get_buckets(const otlp_metric_t *m)
{
	return m ? m->bucket_counts : NULL;
}

int32_t otlp_metric_get_exp_scale(const otlp_metric_t *m) { return m ? m->exp_scale : 0; }
uint64_t otlp_metric_get_exp_zero_count(const otlp_metric_t *m) { return m ? m->exp_zero_count : 0; }
int32_t otlp_metric_get_exp_pos_offset(const otlp_metric_t *m) { return m ? m->exp_pos_offset : 0; }
const uint64_t *otlp_metric_get_exp_pos_counts(const otlp_metric_t *m, size_t *n) { if (n) *n = m ? m->exp_pos_n : 0; return m ? m->exp_pos_counts : NULL; }
int32_t otlp_metric_get_exp_neg_offset(const otlp_metric_t *m) { return m ? m->exp_neg_offset : 0; }
const uint64_t *otlp_metric_get_exp_neg_counts(const otlp_metric_t *m, size_t *n) { if (n) *n = m ? m->exp_neg_n : 0; return m ? m->exp_neg_counts : NULL; }
bool otlp_metric_has_exp_scale(const otlp_metric_t *m) { return m ? m->has_exp_scale : false; }

const struct otlp_attribute *
otlp_metric_get_attrs(const otlp_metric_t *m, size_t *n)
{
	if (n)
		*n = m ? m->n_attrs : 0;
	return m ? m->attrs : NULL;
}

uint8_t otlp_metric_get_agg_temp(const otlp_metric_t *m)
{
	return m ? m->agg_temp : OTLP_AGG_TEMP_CUMULATIVE;
}

bool otlp_metric_get_is_monotonic(const otlp_metric_t *m)
{
	return m ? m->is_monotonic : true;
}

otlp_status_t
otlp_metric_set_aggregation_temporality(otlp_metric_t *m, uint8_t temp)
{
	if (!m)
		return OTLP_ERR_NULL;
	if (temp != OTLP_AGG_TEMP_DELTA && temp != OTLP_AGG_TEMP_CUMULATIVE)
		return OTLP_ERR_INVALID_ARGUMENT;
	m->agg_temp = temp;
	return OTLP_OK;
}

otlp_status_t
otlp_metric_set_monotonic(otlp_metric_t *m, bool monotonic)
{
	if (!m)
		return OTLP_ERR_NULL;
	m->is_monotonic = monotonic;
	return OTLP_OK;
}

otlp_metric_t *
otlp_metric_clone(const otlp_metric_t *src)
{
	struct otlp_metric *dst;

	if (!src)
		return NULL;
	dst = otlp_calloc(1, sizeof(*dst));
	if (!dst)
		return NULL;
	dst->type = src->type;
	dst->name = otlp_dup_str(src->name);
	dst->unit = otlp_dup_str(src->unit);
	dst->description = otlp_dup_str(src->description);
	if (!dst->name || !dst->unit || !dst->description)
		goto fail;
	dst->start_time = src->start_time;
	dst->time = src->time;
	dst->has_start = src->has_start;
	dst->has_time = src->has_time;
	dst->agg_temp = src->agg_temp;
	dst->is_monotonic = src->is_monotonic;
	dst->value = src->value;
	dst->count = src->count;
	dst->sum = src->sum;
	dst->min = src->min;
	dst->max = src->max;
	dst->has_minmax = src->has_minmax;

	/* Attributes */
	if (src->n_attrs > 0)
	{
		if (otlp_attribute_copy_all(dst->attrs, src->attrs,
					    src->n_attrs) != OTLP_OK)
			goto fail;
		dst->n_attrs = src->n_attrs;
	}

	/* Histogram bounds + bucket counts. src was validated at
	 * create but a defensive overflow check is cheap. */
	if (src->n_bounds > 0 && src->bounds)
	{
		if (src->n_bounds > SIZE_MAX / sizeof(double) ||
		    src->n_bounds >= SIZE_MAX / sizeof(uint64_t))
			goto fail;
		dst->bounds = otlp_malloc(src->n_bounds * sizeof(double));
		dst->bucket_counts = otlp_calloc(src->n_bounds + 1,
						 sizeof(uint64_t));
		if (!dst->bounds || !dst->bucket_counts)
			goto fail;
		memcpy(dst->bounds, src->bounds,
		       src->n_bounds * sizeof(double));
		memcpy(dst->bucket_counts, src->bucket_counts,
		       (src->n_bounds + 1) * sizeof(uint64_t));
		dst->n_bounds = src->n_bounds;
	}

	/* ExponentialHistogram */
	dst->exp_scale = src->exp_scale;
	dst->exp_zero_count = src->exp_zero_count;
	dst->exp_pos_offset = src->exp_pos_offset;
	dst->exp_neg_offset = src->exp_neg_offset;
	dst->has_exp_scale = src->has_exp_scale;
	if (src->exp_pos_n > 0 && src->exp_pos_counts)
	{
		if (src->exp_pos_n > SIZE_MAX / sizeof(uint64_t))
			goto fail;
		dst->exp_pos_counts = otlp_calloc(src->exp_pos_n,
						  sizeof(uint64_t));
		if (!dst->exp_pos_counts)
			goto fail;
		memcpy(dst->exp_pos_counts, src->exp_pos_counts,
		       src->exp_pos_n * sizeof(uint64_t));
		dst->exp_pos_n = src->exp_pos_n;
	}
	if (src->exp_neg_n > 0 && src->exp_neg_counts)
	{
		if (src->exp_neg_n > SIZE_MAX / sizeof(uint64_t))
			goto fail;
		dst->exp_neg_counts = otlp_calloc(src->exp_neg_n,
						  sizeof(uint64_t));
		if (!dst->exp_neg_counts)
			goto fail;
		memcpy(dst->exp_neg_counts, src->exp_neg_counts,
		       src->exp_neg_n * sizeof(uint64_t));
		dst->exp_neg_n = src->exp_neg_n;
	}

	return dst;

fail:
	otlp_metric_free(dst);
	return NULL;
}
