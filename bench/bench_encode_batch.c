/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Microbenchmark: batch encode throughput at different batch sizes.
 *
 * Measures how encode cost scales with n_spans. Useful for:
 *   - Detecting O(n^2) patterns in the encoder.
 *   - Verifying the v0.5.38 pre-sized buffer optimization.
 *   - Setting performance baselines for future optimization.
 *
 *   cmake -B build -DOTLP_C_BUILD_BENCH=ON
 *   cmake --build build
 *   ./build/bench/otlp_bench_encode_batch
 */
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include "../src/otlp_messages.h"
#include "../src/protobuf_encode.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t
now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000000000ULL + (uint64_t) ts.tv_nsec;
}

static void
bench_batch(int n_spans, int n_attrs)
{
	otlp_tracer_t     *tracer;
	otlp_span_t      **spans;
	struct otlp_pb_buf  buf;
	otlp_status_t       st;
	uint64_t            t0, t1, elapsed_ns;
	double              ns_per_span;
	int                 i, j;

	tracer = otlp_tracer_create("bench", "bench", "1.0");
	spans  = calloc((size_t) n_spans, sizeof(*spans));

	for (i = 0; i < n_spans; i++)
	{
		spans[i] = otlp_tracer_start_span(tracer, "op");
		for (j = 0; j < n_attrs; j++)
		{
			char k[16];
			snprintf(k, sizeof(k), "k%d", j);
			otlp_span_set_attribute_int(spans[i], k, (int64_t) j);
		}
		otlp_span_mark_end(spans[i]);
	}

	t0 = now_ns();
	st = otlp_pb_buf_init(&buf, 0);
	if (st == OTLP_OK)
		st = otlp_encode_export_trace_service_request(
			&buf, "bench", NULL, NULL, 0, NULL, NULL,
			(const otlp_span_t *const *) spans,
			(size_t) n_spans);
	t1 = now_ns();

	elapsed_ns  = t1 - t0;
	ns_per_span = (double) elapsed_ns / (double) n_spans;

	printf("  batch=%-5d attrs=%-2d  %10" PRIu64 " ns total  %8.1f ns/span  %8zu bytes\n",
	       n_spans, n_attrs, elapsed_ns, ns_per_span, buf.len);

	otlp_pb_buf_free(&buf);

	for (i = 0; i < n_spans; i++)
		otlp_span_free(spans[i]);
	free(spans);
	otlp_tracer_free(tracer);
}

int
main(void)
{
	printf("=== batch encode throughput (protobuf wire) ===\n");
	bench_batch(1, 0);
	bench_batch(1, 5);
	bench_batch(16, 0);
	bench_batch(16, 5);
	bench_batch(64, 0);
	bench_batch(64, 5);
	bench_batch(256, 0);
	bench_batch(256, 5);
	bench_batch(512, 0);
	bench_batch(512, 5);
	printf("=== done ===\n");
	return 0;
}
