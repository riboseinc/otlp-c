/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Property test for the configurable flush_timeout_ms.
 *
 *   prop_flush_timeout_respected — a custom flush_timeout_ms is
 *     honored: flush() returns near the configured deadline even
 *     when pending work remains and the collector is unreachable.
 *
 * Mechanism: null_transport + a status callback that always returns
 * 500 triggers the retry/backoff path without consuming the batch.
 * With backoff_initial_ms set well above flush_timeout_ms, the
 * first retry never fires inside the flush window, so flush() can
 * only exit via the timeout. We measure wall-clock elapsed and
 * assert it is close to the configured timeout — not the 30s
 * default.
 *
 * POSIX-only (uses clock_gettime for timing).
 */
#include "property_harness.h"

#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
int
main(void)
{
	printf("[property] flush-timeout test skipped on Windows\n");
	return 0;
}
#else

#include <time.h>

static uint64_t
mono_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000ULL +
	       (uint64_t) ts.tv_nsec / 1000000ULL;
}

static int
always_500(void *ctx)
{
	(void) ctx;
	return 500;
}

static int
prop_flush_timeout_respected(uint64_t seed)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t     *exp = NULL;
	otlp_tracer_t       *tracer = NULL;
	otlp_span_t         *span;
	uint64_t             t0, t1, elapsed;
	int                  ok = 0;
	otlp_status_t        st;

	(void) seed;
	memset(&opts, 0, sizeof(opts));
	opts.service_name = "timeout-test";
	/* 200 ms cap — well below the 30 s default. */
	opts.flush_timeout_ms = 200;
	/* Don't let retry exhaustion drop the batch before the timeout. */
	opts.max_retries = 1000;
	/* 10 s initial backoff — won't fire within the 200 ms window. */
	opts.backoff_initial_ms = 10000;
	opts.backoff_max_ms = 30000;

	exp = otlp_exporter_create(&opts);
	if (!exp)
		return 0;
	otlp_exporter_set_null_transport(exp, true);
	otlp_exporter_set_null_transport_status_fn(exp, always_500, NULL);

	tracer = otlp_tracer_create("svc", "demo", "1.0");
	if (!tracer)
		goto out;
	span = otlp_tracer_start_span(tracer, "op");
	if (!span)
		goto out;
	otlp_span_mark_end(span);
	st = otlp_exporter_emit(exp, span);
	otlp_span_free(span);
	if (st != OTLP_OK)
		goto out;

	t0 = mono_ms();
	otlp_exporter_flush(exp);
	t1 = mono_ms();
	elapsed = t1 - t0;

	/* Expect: returned near the 200 ms timeout, not the 30 s default.
	 * Allow a generous upper band for CI scheduler jitter. */
	ok = (elapsed >= 150 && elapsed < 5000);

out:
	if (tracer)
		otlp_tracer_free(tracer);
	if (exp)
		otlp_exporter_free(exp);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_flush_timeout_respected,
				 "prop_flush_timeout_respected", 5, 1);

	if (failures)
		printf("[property] %d flush-timeout property(ies) failed\n",
		       failures);
	else
		printf("[property] all flush-timeout properties passed\n");
	return failures ? 1 : 0;
}

#endif /* !_WIN32 */
