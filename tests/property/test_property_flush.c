/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property tests for the metric/log synchronous flush path.
 *
 *   prop_flush_metric_null_transport — flush_metric returns OK with
 *     null_transport mode (no real HTTP).
 *   prop_flush_log_null_transport — same for log records.
 *   prop_flush_metric_variants — counter, gauge, histogram all
 *     flush without error.
 */
#include "property_harness.h"

#include <otlp-c/exporter.h>
#include <otlp-c/log.h>
#include <otlp-c/metric.h>

#include <stdint.h>

static int
prop_flush_metric_null_transport(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t     *exp;
	otlp_metric_t       *m;
	otlp_status_t	    st;
	int		    ok = 0;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "flush-test";
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);

	m = otlp_metric_create(OTLP_METRIC_COUNTER, "requests", "1",
			       "total requests", NULL, 0);
	if (!m)
		goto out;
	otlp_metric_record(m, 5.0);
	otlp_metric_mark_time(m);
	st = otlp_exporter_flush_metric(exp, m);
	ok = (st == OTLP_OK);
	otlp_metric_free(m);
out:
	otlp_exporter_free(exp);
	return ok;
}

static int
prop_flush_log_null_transport(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t     *exp;
	otlp_log_record_t   *lr;
	otlp_status_t	    st;
	int		    ok = 0;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "flush-test";
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);

	lr = otlp_log_record_create(OTLP_SEVERITY_ERROR, "something broke");
	if (!lr)
		goto out;
	otlp_log_record_mark_timestamp(lr);
	otlp_log_record_set_attribute_string(lr, "error_code", "E42");
	st = otlp_exporter_flush_log(exp, lr);
	ok = (st == OTLP_OK);
	otlp_log_record_free(lr);
out:
	otlp_exporter_free(exp);
	return ok;
}

static int
prop_flush_metric_variants(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t     *exp;
	int		    ok = 1;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "flush-test";
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);

	{
		double bounds[3] = {1.0, 10.0, 100.0};
		otlp_metric_type_t types[] = {
			OTLP_METRIC_COUNTER,
			OTLP_METRIC_GAUGE,
			OTLP_METRIC_HISTOGRAM,
			OTLP_METRIC_EXP_HISTOGRAM,
		};
		for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
			otlp_metric_t   *m;
			otlp_status_t    st;

			m = otlp_metric_create(types[i], "m", "", "",
				(types[i] == OTLP_METRIC_HISTOGRAM) ? bounds : NULL,
				(types[i] == OTLP_METRIC_HISTOGRAM) ? 3 : 0);
			if (!m) {
				ok = 0;
				break;
			}
			otlp_metric_record(m, (double)(i + 1));
			otlp_metric_mark_time(m);
			st = otlp_exporter_flush_metric(exp, m);
			if (st != OTLP_OK)
				ok = 0;
			otlp_metric_free(m);
		}
	}
	otlp_exporter_free(exp);
	return ok;
}

static int
prop_flush_exp_histogram_with_buckets(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t     *exp;
	otlp_metric_t       *m;
	otlp_status_t	    st;
	uint64_t	    pos_counts[] = {1, 3, 2};
	int		    ok = 0;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "exp-hist-test";
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);

	m = otlp_metric_create(OTLP_METRIC_EXP_HISTOGRAM, "latency", "ns",
			       "Request latency distribution", NULL, 0);
	if (!m)
		goto out;
	otlp_metric_record(m, 0.001);
	otlp_metric_record(m, 0.002);
	otlp_metric_mark_time(m);
	st = otlp_metric_set_exp_histogram(m,
		20,           /* scale */
		0,            /* pos_offset */
		pos_counts, 3,
		0, NULL, 0);  /* no negative */
	if (st != OTLP_OK)
		goto out_metric;
	st = otlp_exporter_flush_metric(exp, m);
	ok = (st == OTLP_OK);
out_metric:
	otlp_metric_free(m);
out:
	otlp_exporter_free(exp);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_flush_metric_null_transport,
				 "prop_flush_metric_null_transport", 10, 1);
	failures += property_run(prop_flush_log_null_transport,
				 "prop_flush_log_null_transport", 10, 1);
	failures += property_run(prop_flush_metric_variants,
				 "prop_flush_metric_variants", 5, 1);
	failures += property_run(prop_flush_exp_histogram_with_buckets,
				 "prop_flush_exp_histogram_with_buckets", 5, 1);

	if (failures)
		printf("[property] %d flush property(ies) failed\n", failures);
	else
		printf("[property] all flush properties passed\n");
	return failures ? 1 : 0;
}
