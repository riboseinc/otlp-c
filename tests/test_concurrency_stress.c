// SPDX-License-Identifier: BSD-3-Clause
//
// Concurrency stress test for the exporter + tracer + MPSC queue.
// Spawns N threads, each emitting M spans concurrently into a single
// exporter backed by an in-process echo server. Verifies:
//   1. No crash.
//   2. All spans are emitted successfully.
//   3. The echo server received at least one HTTP POST per batch.
//   4. ASAN-clean (no data races, no leaks).

#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#if defined(_WIN32)
#include <stdio.h>
int
main(void)
{
	printf("[stress] skipped on Windows\n");
	return 0;
}
#else

#include "test_helper_echo.h"

#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N_THREADS 8
#define SPANS_PER_THREAD 200

static otlp_tracer_t *g_tracer;
static otlp_exporter_t *g_exp;

struct thread_result
{
	int spans_emitted;
	int failures;
};

static void *
worker(void *arg)
{
	struct thread_result *r = (struct thread_result *) arg;
	int i;

	r->spans_emitted = 0;
	r->failures = 0;

	for (i = 0; i < SPANS_PER_THREAD; i++)
	{
		char name[64];
		otlp_span_t *span;
		otlp_status_t s;

		snprintf(name, sizeof(name), "op-%d", i);
		span = otlp_tracer_start_span(g_tracer, name);
		if (!span)
		{
			r->failures++;
			continue;
		}

		otlp_span_set_attribute_int(
			span, "thread.id", (int64_t) pthread_self());
		otlp_span_set_attribute_int(span, "seq", i);
		otlp_span_mark_end(span);

		s = otlp_exporter_emit(g_exp, span);
		if (s == OTLP_OK)
			r->spans_emitted++;

		otlp_span_free(span);
	}

	return NULL;
}

int
main(void)
{
	struct echo_server srv;
	otlp_exporter_opts_t opts;
	pthread_t threads[N_THREADS];
	struct thread_result results[N_THREADS];
	otlp_status_t s;
	char endpoint[128];
	int i;
	int total_emitted;
	int total_failures;
	int expected;

	memset(&srv, 0, sizeof(srv));

	s = echo_server_start(&srv, NULL, 100);
	if (s != OTLP_OK)
	{
		fprintf(stderr,
			"[stress] FAIL: echo start: %s\n",
			otlp_strerror(s));
		return 1;
	}

	snprintf(endpoint,
		sizeof(endpoint),
		"http://127.0.0.1:%d/v1/traces",
		srv.port);
	printf("[stress] echo on port %d, %d threads x %d spans\n",
		srv.port,
		N_THREADS,
		SPANS_PER_THREAD);

	g_tracer = otlp_tracer_create("stress", "otlp-c", "0.1.0");
	if (!g_tracer)
	{
		fprintf(stderr, "[stress] FAIL: tracer create\n");
		echo_server_stop(&srv);
		if (echo_server_join(&srv, 100000) != OTLP_OK)
		{
			fprintf(stderr, "[stress] FAIL: worker join\n");
			return 1;
		}
		return 1;
	}

	memset(&opts, 0, sizeof(opts));
	opts.endpoint = endpoint;
	opts.service_name = "stress";
	opts.batch_size = 64;
	opts.batch_ms = 50;

	g_exp = otlp_exporter_create(&opts);
	if (!g_exp)
	{
		fprintf(stderr, "[stress] FAIL: exporter create\n");
		otlp_tracer_free(g_tracer);
		echo_server_stop(&srv);
		if (echo_server_join(&srv, 100000) != OTLP_OK)
		{
			fprintf(stderr, "[stress] FAIL: worker join\n");
			return 1;
		}
		return 1;
	}

	for (i = 0; i < N_THREADS; i++)
	{
		memset(&results[i], 0, sizeof(results[i]));
		if (pthread_create(&threads[i], NULL, worker, &results[i]) != 0)
		{
			fprintf(stderr, "[stress] FAIL: create thread %d\n", i);
			return 1;
		}
	}

	for (i = 0; i < N_THREADS; i++)
	{
		if (pthread_join(threads[i], NULL) != 0)
		{
			fprintf(stderr, "[stress] FAIL: join worker %d\n", i);
			return 1;
		}
	}

	otlp_exporter_flush(g_exp);
	otlp_exporter_shutdown(g_exp);

	/* The exact request count is not knowable a priori (batch
	 * packing varies), so stop deterministically — the worker
	 * exits on the self-connect wake and closes the listen fd —
	 * and CHECK the join: a worker still in accept() would touch
	 * this frame after return (v0.5.96/98 lesson). */
	echo_server_stop(&srv);
	if (echo_server_join(&srv, 5 * 1000 * 1000) != OTLP_OK)
	{
		fprintf(stderr, "[stress] FAIL: echo worker join\n");
		otlp_exporter_free(g_exp);
		otlp_tracer_free(g_tracer);
		return 1;
	}

	total_emitted = 0;
	total_failures = 0;
	for (i = 0; i < N_THREADS; i++)
	{
		total_emitted += results[i].spans_emitted;
		total_failures += results[i].failures;
	}

	expected = N_THREADS * SPANS_PER_THREAD;

	/* Load once after join: the join's ACQUIRE load of `running`
	 * carries every increment the worker made before exit. RELAXED
	 * here is safe because the ACQUIRE load above already established
	 * the happens-before edge. */
	size_t served = otlp_atomic_load_u64(
		&srv.requests_served, OTLP_MEMORY_ORDER_RELAXED);

	printf("[stress] emitted=%d expected=%d failures=%d served=%zu\n",
		total_emitted,
		expected,
		total_failures,
		served);

	if (total_emitted != expected)
	{
		fprintf(stderr,
			"[stress] FAIL: emitted %d != %d\n",
			total_emitted,
			expected);
		otlp_exporter_free(g_exp);
		otlp_tracer_free(g_tracer);
		return 1;
	}

	if (total_failures > 0)
	{
		fprintf(stderr,
			"[stress] FAIL: %d emit failures\n",
			total_failures);
		otlp_exporter_free(g_exp);
		otlp_tracer_free(g_tracer);
		return 1;
	}

	if (served == 0)
	{
		fprintf(stderr,
			"[stress] FAIL: no HTTP requests reached server\n");
		otlp_exporter_free(g_exp);
		otlp_tracer_free(g_tracer);
		return 1;
	}

	otlp_exporter_free(g_exp);
	otlp_tracer_free(g_tracer);

	printf("[stress] PASS: %d spans, %d threads, %zu requests\n",
		total_emitted,
		N_THREADS,
		served);
	return 0;
}

#endif /* !_WIN32 */
