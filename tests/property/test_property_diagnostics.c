/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Property tests for the diagnostic callback (otlp_exporter_set_logger).
 *
 *   prop_diag_fires_on_queue_full      — emit() past queue capacity
 *     triggers a WARN log entry with "queue full".
 *   prop_diag_fires_on_4xx_permanent   — null_transport returning 404
 *     triggers an ERROR log entry with "permanent".
 *   prop_diag_fires_on_success         — successful send triggers a
 *     DEBUG log entry with "batch sent".
 *   prop_diag_disabled_by_default      — no callback = no crashes,
 *     zero observable behavior change (NULL check works).
 *
 * Uses a counting logger that records every call; the test then
 * asserts the callback fired the expected number of times with
 * the expected level and message substring.
 */
#include "property_harness.h"

#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_LOG_ENTRIES 64

struct log_sink
{
	struct
	{
		otlp_log_level_t level;
		char message[128];
	} entries[MAX_LOG_ENTRIES];
	size_t n;
};

static void
counting_logger(void *ctx, otlp_log_level_t level, const char *message)
{
	struct log_sink *s = ctx;

	if (s->n < MAX_LOG_ENTRIES)
	{
		s->entries[s->n].level = level;
		snprintf(s->entries[s->n].message,
			sizeof(s->entries[s->n].message),
			"%s",
			message);
		s->n++;
	}
}

static int
count_level(const struct log_sink *s, otlp_log_level_t want)
{
	int count = 0;
	size_t i;

	for (i = 0; i < s->n; i++)
		if (s->entries[i].level == want)
			count++;
	return count;
}

static int
contains_text(const struct log_sink *s,
	otlp_log_level_t level,
	const char *needle)
{
	size_t i;

	for (i = 0; i < s->n; i++)
		if (s->entries[i].level == level &&
			strstr(s->entries[i].message, needle))
			return 1;
	return 0;
}

static int
always_404(void *ctx)
{
	(void) ctx;
	return 404;
}

static int
prop_diag_fires_on_queue_full(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp = NULL;
	otlp_tracer_t *tracer = NULL;
	struct log_sink sink = { 0 };
	int ok = 0;
	otlp_span_t *span;
	int i;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "diag-test";
	opts.queue_capacity = 4;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);
	otlp_exporter_set_logger(exp, counting_logger, &sink);

	tracer = otlp_tracer_create("svc", "demo", "1.0");
	if (!tracer)
		goto out;

	/* Emit 20 spans into a 4-deep queue with no ticking. The queue
	 * fills immediately; subsequent emits return BUFFER_FULL and
	 * fire WARN log entries. */
	for (i = 0; i < 20; i++)
	{
		span = otlp_tracer_start_span(tracer, "op");
		if (!span)
			goto out;
		otlp_exporter_emit(exp, span);
		otlp_span_free(span);
	}

	ok = (count_level(&sink, OTLP_LOG_WARN) > 0 &&
		contains_text(&sink, OTLP_LOG_WARN, "queue full"));

out:
	if (tracer)
		otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	return ok;
}

static int
prop_diag_fires_on_4xx_permanent(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp = NULL;
	otlp_tracer_t *tracer = NULL;
	struct log_sink sink = { 0 };
	otlp_span_t *span;
	int ok = 0;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "diag-test";
	opts.max_retries = 0;
	opts.batch_size = 1;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);
	otlp_exporter_set_null_transport_status_fn(exp, always_404, NULL);
	otlp_exporter_set_logger(exp, counting_logger, &sink);

	tracer = otlp_tracer_create("svc", "demo", "1.0");
	if (!tracer)
		goto out;
	span = otlp_tracer_start_span(tracer, "op");
	if (!span)
		goto out;
	otlp_exporter_emit(exp, span);
	otlp_span_free(span);
	otlp_exporter_tick(exp, 100);

	ok = (count_level(&sink, OTLP_LOG_ERROR) > 0 &&
		contains_text(&sink, OTLP_LOG_ERROR, "permanent"));

out:
	if (tracer)
		otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	return ok;
}

static int
prop_diag_fires_on_success(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp = NULL;
	otlp_tracer_t *tracer = NULL;
	struct log_sink sink = { 0 };
	otlp_span_t *span;
	int ok = 0;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "diag-test";
	opts.batch_size = 1;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);
	otlp_exporter_set_logger(exp, counting_logger, &sink);

	tracer = otlp_tracer_create("svc", "demo", "1.0");
	if (!tracer)
		goto out;
	span = otlp_tracer_start_span(tracer, "op");
	if (!span)
		goto out;
	otlp_exporter_emit(exp, span);
	otlp_span_free(span);
	otlp_exporter_tick(exp, 100);

	ok = (count_level(&sink, OTLP_LOG_DEBUG) > 0 &&
		contains_text(&sink, OTLP_LOG_DEBUG, "batch sent"));

out:
	if (tracer)
		otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	return ok;
}

static int
prop_diag_disabled_by_default(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp = NULL;
	otlp_tracer_t *tracer = NULL;
	otlp_span_t *span;
	int ok = 0;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "diag-test";
	opts.batch_size = 1;
	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);
	/* No logger installed — exercises the NULL-check path. */

	tracer = otlp_tracer_create("svc", "demo", "1.0");
	if (!tracer)
		goto out;
	span = otlp_tracer_start_span(tracer, "op");
	if (!span)
		goto out;
	otlp_exporter_emit(exp, span);
	otlp_span_free(span);
	otlp_exporter_tick(exp, 100);

	/* No crash, no hang — the NULL check in otlp_log prevents any
	 * callback dispatch. This is the zero-overhead default path. */
	ok = 1;

out:
	if (tracer)
		otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_diag_fires_on_queue_full,
		"prop_diag_fires_on_queue_full",
		5,
		1);
	failures += property_run(prop_diag_fires_on_4xx_permanent,
		"prop_diag_fires_on_4xx_permanent",
		5,
		1);
	failures += property_run(
		prop_diag_fires_on_success, "prop_diag_fires_on_success", 5, 1);
	failures += property_run(prop_diag_disabled_by_default,
		"prop_diag_disabled_by_default",
		5,
		1);

	if (failures)
		printf("[property] %d diagnostic property(ies) failed\n",
			failures);
	else
		printf("[property] all diagnostic properties passed\n");
	return failures ? 1 : 0;
}
