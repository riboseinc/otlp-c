/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Shutdown-protocol stress test. Exercises the documented
 * sequence under concurrent emitters:
 *
 *   producers: emit_move() in a loop until they observe
 *              OTLP_ERR_SHUTDOWN (shutdown() is a cooperative
 *              stop signal, NOT a barrier — emits that already
 *              passed the flag check may still land in the queue);
 *   owner:     shutdown() -> tick() drain -> flush() -> free().
 *
 * Under ASAN + LeakSanitizer this pins that the protocol is
 * use-after-free-free and leak-free, and that the accounting
 * invariant (emitted == sent + dropped_err) holds for everything
 * the exporter accepted before shutdown.
 *
 * NOTE: never put side effects in assert() — Release builds
 * define NDEBUG and the expression is NOT evaluated. The first
 * draft called pthread_create inside an assert: under Release no
 * threads were created and pthread_join then read uninitialized
 * stack (the CI segfault); Debug builds masked it. Side-effecting
 * calls get explicit rc checks here.
 */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/tracer.h>

#include "../src/atomic_compat.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
int
main(void)
{
	printf("[shutdown-stress] skipped on Windows\n");
	return 0;
}
#else

static void
nanosleep_(struct timespec *ts)
{
	nanosleep(ts, NULL);
}

#define N_THREADS 4

struct worker_result
{
	uint64_t accepted;
	uint64_t rejected;
	uint64_t full;
	uint64_t null_span;
	uint64_t unexpected;
};

static otlp_exporter_t *g_exp;
static otlp_tracer_t *g_tracer;

static void *
worker(void *arg)
{
	struct worker_result *r = arg;
	int i;

	for (;;)
	{
		otlp_span_t *span = otlp_tracer_start_span(g_tracer, "op");
		otlp_status_t st;

		if (!span)
		{
			r->null_span++;
			break;
		}
		otlp_span_mark_end(span);
		st = otlp_exporter_emit_move(g_exp, span);
		if (st == OTLP_OK)
		{
			r->accepted++;
			continue;
		}
		if (st == OTLP_ERR_SHUTDOWN)
		{
			/* Cooperative stop signal observed. */
			r->rejected++;
			break;
		}
		if (st == OTLP_ERR_BUFFER_FULL)
		{
			/* Back-pressure: the move contract already freed
			 * the span; back off and retry until shutdown. */
			r->full++;
			{
				struct timespec ts = { 0, 100 * 1000 };

				nanosleep_(&ts);
			}
			continue;
		}
		/* Unexpected failure (e.g. NOMEM): stop. */
		r->unexpected++;
		break;
	}
	(void) i;
	return NULL;
}

int
main(void)
{
	otlp_exporter_opts_t opts;
	struct worker_result results[N_THREADS];
	pthread_t threads[N_THREADS];
	otlp_exporter_stats_t stats;
	uint64_t total_accepted = 0;
	uint64_t total_rejected = 0;
	int i;

	memset(&opts, 0, sizeof(opts));
	opts.service_name = "shutdown-stress";
	opts.batch_size = 64;
	opts.batch_ms = 0;
	opts.queue_capacity = 256;

	g_exp = otlp_exporter_create(&opts);
	assert(g_exp != NULL);
	otlp_exporter_set_null_transport(g_exp, true);
	g_tracer = otlp_tracer_create("svc", "lib", "1.0");
	assert(g_tracer != NULL);

	for (i = 0; i < N_THREADS; i++)
	{
		int rc;

		memset(&results[i], 0, sizeof(results[i]));
		rc = pthread_create(&threads[i], NULL, worker, &results[i]);
		if (rc != 0)
		{
			printf("[shutdown-stress] pthread_create(%d) failed: "
			       "%d\n",
				i,
				rc);
			return 1;
		}
	}

	/* Let the producers run concurrently with the shutdown: this
	 * is the race the protocol must survive. */
	for (i = 0; i < 200; i++)
	{
		struct timespec ts = { 0, 1000 * 1000 };

		nanosleep_(&ts);
		(void) otlp_exporter_tick(g_exp, 0);
	}

	otlp_exporter_shutdown(g_exp);
	/* Producers now observe ERR_SHUTDOWN and stop on their own. */
	for (i = 0; i < N_THREADS; i++)
		pthread_join(threads[i], NULL);

	/* Drain per the contract: tick until idle, then flush. */
	for (i = 0; i < 1000; i++)
		if (otlp_exporter_tick(g_exp, 0) != OTLP_OK)
			break;
	otlp_exporter_flush(g_exp);

	otlp_exporter_get_stats(g_exp, &stats);
	for (i = 0; i < N_THREADS; i++)
	{
		total_accepted += results[i].accepted;
		total_rejected += results[i].rejected;
	}

	/* Everything the exporter accepted was accounted: sent or
	 * dropped. Items drained by free() after shutdown also count
	 * as accepted-by-the-queue here only if they were counted as
	 * emitted — the invariant the stats contract guarantees. */
	printf("[shutdown-stress] null_span=%llu unexpected=%llu\n",
		(unsigned long long) results[0].null_span,
		(unsigned long long) results[0].unexpected);
	printf("[shutdown-stress] accepted=%llu rejected=%llu "
	       "full(thread0)=%llu\n",
		(unsigned long long) total_accepted,
		(unsigned long long) total_rejected,
		(unsigned long long) results[0].full);
	printf("[shutdown-stress] emitted=%llu sent=%llu full=%llu err=%llu\n",
		(unsigned long long) stats.emitted,
		(unsigned long long) stats.sent,
		(unsigned long long) stats.dropped_full,
		(unsigned long long) stats.dropped_err);
	{
		size_t k;
		for (k = 0; k < N_THREADS; k++)
			printf("[shutdown-stress] t%zu: acc=%llu rej=%llu "
			       "full=%llu\n",
				k,
				(unsigned long long) results[k].accepted,
				(unsigned long long) results[k].rejected,
				(unsigned long long) results[k].full);
	}
	otlp_tracer_free(g_tracer);
	otlp_exporter_free(g_exp);

	if (total_accepted == 0)
	{
		printf("[shutdown-stress] FAIL (nothing accepted)\n");
		return 1;
	}
	if (stats.emitted != total_accepted)
	{
		printf("[shutdown-stress] FAIL (emitted != accepted)\n");
		return 1;
	}
	/* The stats contract (v0.5.59): everything emitted is
	 * eventually sent or dropped-due-to-error. dropped_full
	 * counts REJECTED emits (never counted in emitted). */
	if (stats.emitted != stats.sent + stats.dropped_err)
	{
		printf("[shutdown-stress] FAIL (emitted != sent + "
		       "dropped_err)\n");
		return 1;
	}
	if (total_rejected == 0)
	{
		/* With 20000 iterations per thread and shutdown mid-run,
		 * every producer must have observed the stop signal. */
		printf("[shutdown-stress] FAIL (no producer saw "
		       "ERR_SHUTDOWN)\n");
		return 1;
	}
	printf("[shutdown-stress] PASS\n");
	return 0;
}

#endif
