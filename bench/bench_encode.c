/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Microbenchmark: encode N spans, time the wall clock per call.
 * Not a regression benchmark — just a baseline measurement tool.
 *
 *   cmake -B build -DOTLP_C_BUILD_TESTS=ON
 *   cmake --build build
 *   ./build/bench/otlp_bench_encode
 *
 * Output: spans/sec + ns/op for each (n_spans, n_attrs) combination.
 */
#include <otlp-c/otlp.h>
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
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void
build_span_with_attrs(otlp_tracer_t *t, int n_attrs)
{
	otlp_span_t *s = otlp_tracer_start_span(t, "op");

	for (int i = 0; i < n_attrs; i++) {
		char k[16];

		snprintf(k, sizeof(k), "k%d", i);
		otlp_span_set_attribute_int(s, k, (int64_t)i);
	}
	otlp_span_mark_end(s);
	/* Caller is responsible for free; we drop on the floor for the
	 * bench since this is just measuring encode cost. */
	otlp_span_free(s);
}

static void
bench_encode(int n_spans, int n_attrs)
{
	otlp_tracer_t    *t = otlp_tracer_create("bench", "bench", "0");
	struct otlp_pb_buf buf;
	otlp_span_t	    **spans;
	uint64_t	     t0, t1;

	spans = calloc((size_t)n_spans, sizeof(*spans));
	for (int i = 0; i < n_spans; i++) {
		spans[i] = otlp_tracer_start_span(t, "op");
		for (int a = 0; a < n_attrs; a++) {
			char k[16];
			snprintf(k, sizeof(k), "k%d", a);
			otlp_span_set_attribute_int(spans[i], k, (int64_t)a);
		}
		otlp_span_mark_end(spans[i]);
	}

	otlp_pb_buf_init(&buf, 0);
	t0 = now_ns();
	otlp_encode_export_trace_service_request(
	    &buf, "bench", "bench", "0",
	    (const otlp_span_t *const *)spans, (size_t)n_spans);
	t1 = now_ns();

	uint64_t ns	  = t1 - t0;
	double    ns_per = (double)ns / n_spans;
	double    per_s  = (double)n_spans * 1e9 / (double)ns;

	printf("  encode %4d spans × %2d attrs: %8" PRIu64
	       " ns total, %6.1f ns/span, %10.0f spans/s\n",
	       n_spans, n_attrs, ns, ns_per, per_s);

	otlp_pb_buf_free(&buf);
	for (int i = 0; i < n_spans; i++)
		otlp_span_free(spans[i]);
	free(spans);
	otlp_tracer_free(t);
}

int
main(void)
{
	printf("[bench] otlp-c %s — encode microbenchmark\n", otlp_version());

	/* Warmup. */
	bench_encode(100, 1);

	printf("\n[bench] encode cost vs n_spans (1 attr each):\n");
	for (int n = 1; n <= 1000; n *= 10)
		bench_encode(n, 1);

	printf("\n[bench] encode cost vs n_attrs (100 spans each):\n");
	for (int a = 0; a <= 32; a = (a == 0) ? 1 : a * 2)
		bench_encode(100, a);

	return 0;
}
