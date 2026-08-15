/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Microbenchmark: log emit + tick throughput.
 *
 * Mirrors bench_emit.c for the logs signal: measures the full
 * pipeline (record clone in emit, MPSC push, tick drain, encode,
 * null_transport "send") for both the clone API (emit_log) and the
 * move API (emit_log_move).
 *
 *   cmake -B build -DOTLP_C_BUILD_BENCH=ON
 *   cmake --build build
 *   ./build/bench/otlp_bench_logs
 */
#include <otlp-c/exporter.h>
#include <otlp-c/log.h>

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
bench(int n_logs, int n_attrs)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_t *exp;
	uint64_t t0, t1;
	double ns_clone, ns_move;
	int i, j;

	memset(&opts, 0, sizeof(opts));
	opts.service_name = "bench";
	opts.batch_size = (size_t) n_logs;
	opts.batch_ms = 0;
	opts.queue_capacity = (size_t) n_logs * 2;

	exp = otlp_exporter_create(&opts);
	if (!exp)
	{
		fprintf(stderr, "exporter create failed\n");
		return;
	}
	otlp_exporter_set_null_transport(exp, true);

	/* Pass 1: emit_log (clone + queue + tick). Each emit deep-copies
	 * the record, so this measures the cost a caller using the clone
	 * API pays per log record. */
	{
		otlp_log_record_t *tmpl =
			otlp_log_record_create(OTLP_SEVERITY_INFO, "message");

		for (j = 0; j < n_attrs; j++)
		{
			char k[16];
			snprintf(k, sizeof(k), "key%d", j);
			otlp_log_record_set_attribute_int(tmpl, k, (int64_t) j);
		}

		t0 = now_ns();
		for (i = 0; i < n_logs; i++)
			otlp_exporter_emit_log(exp, tmpl);
		otlp_exporter_flush(exp);
		t1 = now_ns();
		ns_clone = (double) (t1 - t0) / (double) n_logs;
		otlp_log_record_free(tmpl);
	}

	/* Pass 2: emit_log_move (queue + tick only, no clone). The record
	 * is built fresh per iteration (the move variant takes ownership).
	 * The clone-vs-move delta is the deep-copy cost the clone API
	 * adds. */
	{
		t0 = now_ns();
		for (i = 0; i < n_logs; i++)
		{
			otlp_log_record_t *lr = otlp_log_record_create(
				OTLP_SEVERITY_INFO, "message");

			for (j = 0; j < n_attrs; j++)
			{
				char k[16];
				snprintf(k, sizeof(k), "key%d", j);
				otlp_log_record_set_attribute_int(
					lr, k, (int64_t) j);
			}
			otlp_exporter_emit_log_move(exp, lr);
		}
		otlp_exporter_flush(exp);
		t1 = now_ns();
		ns_move = (double) (t1 - t0) / (double) n_logs;
	}

	printf("  logs=%-6d attrs=%-3d  %10.1f ns/log (emit)  "
	       "%10.1f ns/log (build+move)  %8.0f/%8.0f logs/s\n",
		n_logs,
		n_attrs,
		ns_clone,
		ns_move,
		1e9 / ns_clone,
		1e9 / ns_move);

	otlp_exporter_free(exp);
}

int
main(void)
{
	printf("=== log emit + tick throughput (null_transport) ===\n");
	bench(1000, 0);
	bench(1000, 1);
	bench(1000, 5);
	bench(1000, 10);
	bench(5000, 0);
	printf("=== done ===\n");
	return 0;
}
