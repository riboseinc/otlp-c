// SPDX-License-Identifier: BSD-3-Clause
//
// Multi-signal concurrency stress test. Exercises the v0.5.28
// async metric/log pipeline under concurrent load: N threads emit
// spans, metrics, AND logs into one exporter while the main thread
// ticks. Verifies all items reach the null-transport "send" path
// and per-signal stats are correct.
//
// Run under TSAN to verify the three-queue, one-in-flight,
// shared-backoff design is race-free.

#if !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#if defined(_WIN32)
#include <stdio.h>
int main(void) { printf("[stress] skipped on Windows\n"); return 0; }
#else

#include <otlp-c/exporter.h>
#include <otlp-c/log.h>
#include <otlp-c/metric.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define N_THREADS 8
#define ITEMS_PER_THREAD 100

static otlp_tracer_t  *g_tracer;
static otlp_exporter_t *g_exp;

struct thread_result {
	int spans;
	int metrics;
	int logs;
	int failures;
};

static void *
worker(void *arg)
{
	struct thread_result *r = (struct thread_result *)arg;
	int i;

	r->spans = 0;
	r->metrics = 0;
	r->logs = 0;
	r->failures = 0;

	for (i = 0; i < ITEMS_PER_THREAD; i++) {
		char name[32];

		/* Emit a span. */
		snprintf(name, sizeof(name), "op-%d", i);
		otlp_span_t *span = otlp_tracer_start_span(g_tracer, name);
		if (span) {
			otlp_span_set_attribute_int(span, "seq", i);
			otlp_span_mark_end(span);
			if (otlp_exporter_emit(g_exp, span) == OTLP_OK)
				r->spans++;
			otlp_span_free(span);
		}

		/* Emit a metric (move — exporter takes ownership). */
		snprintf(name, sizeof(name), "metric-%d", i);
		otlp_metric_t *m = otlp_metric_create(
			OTLP_METRIC_COUNTER, name, "1", "", NULL, 0);
		if (m) {
			otlp_metric_record(m, (double)i);
			otlp_metric_mark_time(m);
			if (otlp_exporter_emit_metric_move(g_exp, m) == OTLP_OK)
				r->metrics++;
		}

		/* Emit a log (move — exporter takes ownership). */
		snprintf(name, sizeof(name), "log-%d", i);
		otlp_log_record_t *lr = otlp_log_record_create(
			OTLP_SEVERITY_INFO, name);
		if (lr) {
			otlp_log_record_mark_timestamp(lr);
			if (otlp_exporter_emit_log_move(g_exp, lr) == OTLP_OK)
				r->logs++;
		}
	}

	return NULL;
}

int main(void)
{
	otlp_exporter_opts_t opts;
	otlp_exporter_stats_t stats;
	pthread_t threads[N_THREADS];
	struct thread_result results[N_THREADS];
	int i;
	int total_spans = 0, total_metrics = 0, total_logs = 0;
	int expected = N_THREADS * ITEMS_PER_THREAD;

	memset(&opts, 0, sizeof(opts));
	opts.service_name = "multi-signal-stress";
	opts.batch_size = 32;
	opts.batch_ms = 10;
	opts.queue_capacity = 4096;

	g_exp = otlp_exporter_create(&opts);
	if (!g_exp) {
		fprintf(stderr, "[stress] FAIL: exporter create\n");
		return 1;
	}
	otlp_exporter_set_null_transport(g_exp, true);

	g_tracer = otlp_tracer_create("stress", "otlp-c", "0.5.34");
	if (!g_tracer) {
		fprintf(stderr, "[stress] FAIL: tracer create\n");
		otlp_exporter_free(g_exp);
		return 1;
	}

	printf("[stress] %d threads x %d items (span+metric+log) = %d per signal\n",
	       N_THREADS, ITEMS_PER_THREAD, expected);

	/* Spawn workers. */
	for (i = 0; i < N_THREADS; i++) {
		memset(&results[i], 0, sizeof(results[i]));
		if (pthread_create(&threads[i], NULL, worker, &results[i]) != 0) {
			fprintf(stderr, "[stress] FAIL: create thread %d\n", i);
			return 1;
		}
	}

	/* Wait for all workers. */
	for (i = 0; i < N_THREADS; i++)
		pthread_join(threads[i], NULL);

	/* Flush to drain all three queues. */
	otlp_exporter_flush(g_exp);

	/* Collect stats. */
	otlp_exporter_get_stats(g_exp, &stats);

	for (i = 0; i < N_THREADS; i++) {
		total_spans += results[i].spans;
		total_metrics += results[i].metrics;
		total_logs += results[i].logs;
	}

	printf("[stress] emitted: spans=%d metrics=%d logs=%d\n",
	       total_spans, total_metrics, total_logs);
	printf("[stress] stats: emitted=%llu sent=%llu "
	       "emitted_metrics=%llu sent_metrics=%llu "
	       "emitted_logs=%llu sent_logs=%llu\n",
	       (unsigned long long)stats.emitted,
	       (unsigned long long)stats.sent,
	       (unsigned long long)stats.emitted_metrics,
	       (unsigned long long)stats.sent_metrics,
	       (unsigned long long)stats.emitted_logs,
	       (unsigned long long)stats.sent_logs);

	otlp_tracer_free(g_tracer);
	otlp_exporter_free(g_exp);

	if (total_spans != expected) {
		fprintf(stderr, "[stress] FAIL: spans %d != %d\n",
			total_spans, expected);
		return 1;
	}
	if (total_metrics != expected) {
		fprintf(stderr, "[stress] FAIL: metrics %d != %d\n",
			total_metrics, expected);
		return 1;
	}
	if (total_logs != expected) {
		fprintf(stderr, "[stress] FAIL: logs %d != %d\n",
			total_logs, expected);
		return 1;
	}
	if (stats.sent != (uint64_t)expected) {
		fprintf(stderr, "[stress] FAIL: sent %llu != %d\n",
			(unsigned long long)stats.sent, expected);
		return 1;
	}
	if (stats.sent_metrics != (uint64_t)expected) {
		fprintf(stderr, "[stress] FAIL: sent_metrics %llu != %d\n",
			(unsigned long long)stats.sent_metrics, expected);
		return 1;
	}
	if (stats.sent_logs != (uint64_t)expected) {
		fprintf(stderr, "[stress] FAIL: sent_logs %llu != %d\n",
			(unsigned long long)stats.sent_logs, expected);
		return 1;
	}

	printf("[stress] PASS: %d spans + %d metrics + %d logs, "
	       "all sent\n", total_spans, total_metrics, total_logs);
	return 0;
}

#endif /* !_WIN32 */
