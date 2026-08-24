/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * MPSC queue contention test. N producer threads push M items each
 * into a single queue; the main thread drains. Verifies:
 *   - every item arrives exactly once (no losses, no duplicates)
 *   - total drained == N*M
 *   - ASAN/TSan-clean
 */
#include "prng.h"
#include "property_harness.h"

#include "../src/mpsc_queue.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MPSC_QUEUE_CAP    4096  /* must be power of 2 */
#define N_PRODUCERS	      4
#define ITEMS_PER_PRODUCER 250

struct producer_arg {
	struct mpsc_queue *q;
	int		  producer_id;
	_Atomic int	  *done_flag;
};

static void *
producer_main(void *arg)
{
	struct producer_arg *pa = arg;
	int		     i;

	for (i = 0; i < ITEMS_PER_PRODUCER; i++) {
		/* Encode producer_id in low bits, sequence in high bits
		 * so the consumer can verify uniqueness + completeness. */
		uintptr_t item =
		    (uintptr_t)(uint64_t)(i + 1) << 16 |
		    (uintptr_t)(pa->producer_id + 1);
		otlp_status_t st;

		do {
			st = mpsc_queue_push(pa->q, (void *)item);
			if (st == OTLP_ERR_BUFFER_FULL)
				sched_yield();
		} while (st == OTLP_ERR_BUFFER_FULL);
		if (st != OTLP_OK) {
			fprintf(stderr,
				"[mpsc] producer %d push failed: %s\n",
				pa->producer_id, otlp_strerror(st));
			atomic_store_explicit(pa->done_flag, 1,
			    memory_order_release);
			return (void *)1;
		}
	}
	return NULL;
}

static int
prop_mpsc_no_loss_no_dup(uint64_t seed)
{
	struct mpsc_queue   q;
	pthread_t	    threads[N_PRODUCERS];
	struct producer_arg args[N_PRODUCERS];
	void		   *thread_rc;
	int		    i;
	int		    rc = 1;
	uint64_t	   *seen;
	int		    n_seen = 0;
	int		    n_dup = 0;
	void		   *item;
	_Atomic int	    producers_done = 0;

	(void)seed;

	if (mpsc_queue_init(&q, MPSC_QUEUE_CAP) != OTLP_OK)
		return 0;

	for (i = 0; i < N_PRODUCERS; i++) {
		args[i].q = &q;
		args[i].producer_id = i;
		args[i].done_flag = &producers_done;
		if (pthread_create(&threads[i], NULL, producer_main,
				   &args[i]) != 0)
			goto out;
	}

	seen = calloc((size_t)N_PRODUCERS * ITEMS_PER_PRODUCER,
		      sizeof(*seen));
	if (!seen)
		goto out;

	/* Drain concurrently with producers. Yield when queue is
	 * momentarily empty to avoid pegging the CPU against them. */
	while (n_seen + n_dup < N_PRODUCERS * ITEMS_PER_PRODUCER) {
		item = mpsc_queue_pop(&q);
		if (!item) {
			sched_yield();
			continue;
		}
		uint64_t v = (uint64_t)(uintptr_t)item;
		uint64_t producer = v & 0xFFFF;
		uint64_t seq	   = v >> 16;
		if (producer < 1 || producer > N_PRODUCERS) {
			fprintf(stderr, "[mpsc] bad producer id %llu\n",
				(unsigned long long)producer);
			goto out_seen;
		}
		if (seq < 1 || seq > ITEMS_PER_PRODUCER) {
			fprintf(stderr,
				"[mpsc] bad seq %llu (producer %llu)\n",
				(unsigned long long)seq,
				(unsigned long long)producer);
			goto out_seen;
		}
		uint64_t flat = (producer - 1) * ITEMS_PER_PRODUCER + (seq - 1);
		if (seen[flat]) {
			n_dup++;
			continue;
		}
		seen[flat] = flat + 1;  /* nonzero marker */
		n_seen++;
	}

	for (i = 0; i < N_PRODUCERS; i++) {
		pthread_join(threads[i], &thread_rc);
		if (thread_rc != NULL) {
			fprintf(stderr, "[mpsc] producer %d errored\n", i);
			goto out_seen;
		}
	}

	/* Drain any final items. */
	while ((item = mpsc_queue_pop(&q)) != NULL) {
		uint64_t v = (uint64_t)(uintptr_t)item;
		uint64_t producer = v & 0xFFFF;
		uint64_t seq	   = v >> 16;
		uint64_t flat =
		    (producer - 1) * ITEMS_PER_PRODUCER + (seq - 1);
		if (seen[flat]) {
			n_dup++;
			continue;
		}
		seen[flat] = flat + 1;
		n_seen++;
	}

	rc = (n_seen == N_PRODUCERS * ITEMS_PER_PRODUCER && n_dup == 0) ? 1 : 0;
	if (!rc)
		fprintf(stderr,
			"[mpsc] seen=%d dup=%d (expected %d)\n",
			n_seen, n_dup,
			N_PRODUCERS * ITEMS_PER_PRODUCER);

out_seen:
	free(seen);
out:
	mpsc_queue_free(&q);
	return rc;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_mpsc_no_loss_no_dup,
				 "prop_mpsc_no_loss_no_dup", 5, 1);

	if (failures)
		printf("[property] %d mpsc property(ies) failed\n",
		       failures);
	else
		printf("[property] all mpsc properties passed\n");
	return failures ? 1 : 0;
}

