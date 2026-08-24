/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Multi-threaded usage example.
 *
 * Demonstrates the library's core embedding pattern:
 *   - N worker threads call otlp_exporter_emit() concurrently
 *     (lock-free MPSC queue; safe from any thread).
 *   - One dedicated "tick" thread drains the queue and drives the
 *     in-flight HTTP POST. The library never spawns a thread —
 *     the caller owns this one.
 *   - Clean shutdown: workers join, then the tick thread drains
 *     the remaining queue via flush(). This ordering is the
 *     documented concurrency contract (v0.5.82): shutdown() is a
 *     cooperative stop signal, NOT a barrier, and free() requires
 *     that no emit is executing or called afterwards — joining
 *     the workers first is what makes flush()+free() safe.
 *
 * The workers use emit() (the library deep-copies; the span can
 * be freed immediately after). For hot paths, emit_move()
 * transfers ownership and skips the clone — same thread-safety.
 *
 * Uses null_transport mode so it runs without a local otelcol —
 * remove the set_null_transport call to emit to a real collector.
 *
 *   cmake -B build -DOTLP_C_BUILD_EXAMPLES=ON
 *   cmake --build build
 *   ./build/examples/otlp_example_multithread
 *
 * On platforms without pthread (Windows MSVC builds without POSIX),
 * the example prints a "skipped" message and exits 0.
 */
#include <otlp-c/otlp.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
typedef HANDLE otlp_thread_t;
typedef struct
{
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	int thread_id;
	int n;
} worker_arg_t;
static DWORD WINAPI
worker_thread(LPVOID p)
{
	worker_arg_t *a = p;
	for (int i = 0; i < a->n; i++)
	{
		char name[64];
		otlp_span_t *s;
		snprintf(name, sizeof(name), "op-%d-t%d", i, a->thread_id);
		s = otlp_tracer_start_span(a->tracer, name);
		if (!s)
			continue;
		otlp_span_set_attribute_int(s, "thread.id", a->thread_id);
		otlp_span_set_attribute_int(s, "seq", i);
		otlp_span_mark_end(s);
		otlp_exporter_emit(a->exp, s);
		otlp_span_free(s);
	}
	return 0;
}
static otlp_thread_t
thread_create_worker(worker_arg_t *a)
{
	return CreateThread(NULL, 0, worker_thread, a, 0, NULL);
}
static void
thread_join(otlp_thread_t t)
{
	WaitForSingleObject(t, INFINITE);
	CloseHandle(t);
}
#else
#include <pthread.h>
typedef pthread_t otlp_thread_t;
typedef struct
{
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	int thread_id;
	int n;
} worker_arg_t;
static void *
worker_thread(void *p)
{
	worker_arg_t *a = p;
	for (int i = 0; i < a->n; i++)
	{
		char name[64];
		otlp_span_t *s;
		snprintf(name, sizeof(name), "op-%d-t%d", i, a->thread_id);
		s = otlp_tracer_start_span(a->tracer, name);
		if (!s)
			continue;
		otlp_span_set_attribute_int(s, "thread.id", a->thread_id);
		otlp_span_set_attribute_int(s, "seq", i);
		otlp_span_mark_end(s);
		otlp_exporter_emit(a->exp, s);
		otlp_span_free(s);
	}
	return NULL;
}
static otlp_thread_t
thread_create_worker(worker_arg_t *a)
{
	otlp_thread_t t;
	pthread_create(&t, NULL, worker_thread, a);
	return t;
}
static void
thread_join(otlp_thread_t t)
{
	pthread_join(t, NULL);
}
#endif

#define N_WORKERS 4
#define SPANS_PER_WORKER 250

typedef struct
{
	otlp_exporter_t *exp;
	volatile int *should_stop;
} tick_arg_t;

#if !defined(_WIN32)
/* Tick thread: loops tick() until the main thread sets should_stop.
 * On Windows we skip the background tick thread (the example still
 * demonstrates multi-threaded emit; the main thread ticks after
 * workers join). */
static void *
tick_thread(void *p)
{
	tick_arg_t *a = p;
	while (!*a->should_stop)
		otlp_exporter_tick(a->exp, 10);
	return NULL;
}
#endif

int
main(void)
{
	otlp_exporter_opts_t opts = { 0 };
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	otlp_thread_t workers[N_WORKERS];
	worker_arg_t wargs[N_WORKERS];
	otlp_exporter_stats_t stats;
	int total_expected;
	volatile int should_stop = 0;

	/* Resource attributes describing the process. */
	otlp_resource_attr_t rsrc[2];
	rsrc[0].key = "service.version";
	rsrc[0].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "1.0.0" } };
	rsrc[1].key = "deployment.environment";
	rsrc[1].value = (otlp_value_t){ .type = OTLP_VALUE_STRING,
		.v = { .string_val = "demo" } };

	opts.service_name = "multithread-demo";
	opts.resource_attributes = rsrc;
	opts.n_resource_attributes = 2;
	opts.batch_size = 64;
	opts.batch_ms = 50;
	/* Keep flush bounded for the demo. */
	opts.flush_timeout_ms = 5000;

	exp = otlp_exporter_create(&opts);
	if (!exp)
	{
		fprintf(stderr, "exporter create failed\n");
		return 1;
	}
	/* null_transport: runs without a local collector. */
	otlp_exporter_set_null_transport(exp, true);

	tracer = otlp_tracer_create("multithread-demo", "demo", otlp_version());
	if (!tracer)
	{
		fprintf(stderr, "tracer create failed\n");
		otlp_exporter_free(exp);
		return 1;
	}

	printf("[multithread] launching %d workers x %d spans each\n",
		N_WORKERS,
		SPANS_PER_WORKER);

	/* Spawn workers — they all emit concurrently into one exporter. */
	for (int i = 0; i < N_WORKERS; i++)
	{
		wargs[i].exp = exp;
		wargs[i].tracer = tracer;
		wargs[i].thread_id = i;
		wargs[i].n = SPANS_PER_WORKER;
		workers[i] = thread_create_worker(&wargs[i]);
	}

#if !defined(_WIN32)
	/* Tick thread runs concurrently with the workers. */
	{
		tick_arg_t ta;
		pthread_t tt;
		ta.exp = exp;
		ta.should_stop = &should_stop;
		pthread_create(&tt, NULL, tick_thread, &ta);

		/* Wait for workers to finish emitting. */
		for (int i = 0; i < N_WORKERS; i++)
			thread_join(workers[i]);

		/* Signal tick thread to stop, then join it. */
		should_stop = 1;
		pthread_join(tt, NULL);
	}
#else
	/* Windows: tick in-line after workers join (no background tick thread
	 * in this demo to avoid pulling in Windows threading primitives for
	 * a periodic loop). */
	for (int i = 0; i < N_WORKERS; i++)
		thread_join(workers[i]);
#endif

	/* Final flush drains anything left in the queue. */
	otlp_exporter_flush(exp);

	otlp_exporter_get_stats(exp, &stats);
	total_expected = N_WORKERS * SPANS_PER_WORKER;
	printf("[multithread] emitted=%llu sent=%llu (expected %d)\n",
		(unsigned long long) stats.emitted,
		(unsigned long long) stats.sent,
		total_expected);

	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);

	if (stats.emitted != (uint64_t) total_expected)
	{
		fprintf(stderr,
			"[multithread] FAIL: emitted %llu != %d\n",
			(unsigned long long) stats.emitted,
			total_expected);
		return 1;
	}
	printf("[multithread] PASS\n");
	return 0;
}
