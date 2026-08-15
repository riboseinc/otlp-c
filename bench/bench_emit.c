/* SPDX-License-Identifier-Identifier: Apache-2.0 */
/*
 * Microbenchmark: emit + tick throughput.
 *
 * Measures the full pipeline: span clone (in emit), MPSC push,
 * tick drain, encode, null_transport "send". Isolates the
 * library's internal cost from network I/O.
 *
 *   cmake -B build -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_BUILD_BENCH=ON
 *   cmake --build build
 *   ./build/bench/otlp_bench_emit
 *
 * Output: spans/sec + ns/op for each (n_spans, n_attrs) combination.
 */
#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t
now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

static void
bench(int n_spans, int n_attrs)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	uint64_t t0, t1;
	double ns_clone, ns_move;
	int i, j;

	memset(&opts, 0, sizeof(opts));
	opts.service_name = "bench";
	opts.batch_size = (size_t) n_spans; /* flush in one batch */
	opts.batch_ms = 0; /* don't wait */
	opts.queue_capacity = (size_t) n_spans * 2;

	exp = otlp_exporter_create(&opts);
	if (!exp)
	{
		fprintf(stderr, "exporter create failed\n");
		return;
	}
	otlp_exporter_set_null_transport(exp, true);

	tracer = otlp_tracer_create("bench", "bench", "1.0");

	/* Pass 1: emit (clone + queue + tick). Each emit deep-copies
	 * the span, so this measures the full cost a caller using the
	 * clone API pays per span. */
	{
		otlp_span_t *tmpl = otlp_tracer_start_span(tracer, "operation");

		for (j = 0; j < n_attrs; j++)
		{
			char k[16];
			snprintf(k, sizeof(k), "key%d", j);
			otlp_span_set_attribute_int(tmpl, k, (int64_t) j);
		}
		otlp_span_mark_end(tmpl);

		t0 = now_ns();
		for (i = 0; i < n_spans; i++)
			otlp_exporter_emit(exp, tmpl);
		otlp_exporter_flush(exp);
		t1 = now_ns();
		ns_clone = (double) (t1 - t0) / (double) n_spans;
		otlp_span_free(tmpl);
	}

	/* Pass 2: emit_move (queue + tick only, no clone). The span
	 * is built fresh per iteration (the move variant takes
	 * ownership), so start_span cost is included — but no deep
	 * copy of a fully-populated template. The clone-vs-move delta
	 * is the deep-copy cost the clone API adds. */
	{
		t0 = now_ns();
		for (i = 0; i < n_spans; i++)
		{
			otlp_span_t *s =
				otlp_tracer_start_span(tracer, "operation");

			for (j = 0; j < n_attrs; j++)
			{
				char k[16];
				snprintf(k, sizeof(k), "key%d", j);
				otlp_span_set_attribute_int(s, k, (int64_t) j);
			}
			otlp_span_mark_end(s);
			otlp_exporter_emit_move(exp, s);
		}
		otlp_exporter_flush(exp);
		t1 = now_ns();
		ns_move = (double) (t1 - t0) / (double) n_spans;
	}

	printf("  spans=%-6d attrs=%-3d  %10.1f ns/span (emit)  "
	       "%10.1f ns/span (build+move)  %8.0f/%8.0f spans/s\n",
		n_spans,
		n_attrs,
		ns_clone,
		ns_move,
		1e9 / ns_clone,
		1e9 / ns_move);

	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
}

int
main(void)
{
	printf("=== emit + tick throughput (null_transport) ===\n");
	bench(1000, 0);
	bench(1000, 1);
	bench(1000, 5);
	bench(1000, 10);
	bench(5000, 0);
	bench(5000, 5);
	printf("=== done ===\n");
	return 0;
}
