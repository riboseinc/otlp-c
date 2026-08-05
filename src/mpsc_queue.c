/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Lock-free bounded MPSC ring. See src/mpsc_queue.h.
 *
 * Per-slot sequence-number scheme (Dmitry Vyukov's bounded MPMC):
 *
 *   Producer claiming slot at index h:
 *     - read slot.seq; expect == h+1 (slot is empty for this turn)
 *     - CAS slot.seq from h+1 to h+1+mask (slot claimed)
 *     - write slot.data
 *     - CAS slot.seq from h+1+mask to h+1+mask+1 (slot published)
 *
 *   Consumer at index t:
 *     - read slot.seq; expect == t+1+mask+1 (slot is published)
 *     - read slot.data
 *     - store slot.seq = t+1+mask+1+1 = t+mask+2 (slot is empty for next turn)
 *
 * The sequence number's lower bits encode "whose turn" the slot
 * belongs to; the upper bits encode state transitions. This handles
 * the producer-wins-CAS-then-writes race correctly: the consumer
 * won't read the slot until the sequence is "published."
 *
 * For MPSC this is overkill (works for MPMC) but it's a well-trodden
 * pattern with no ABA on a bounded ring.
 */
#include "mpsc_queue.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static bool
is_pow2(size_t n)
{
	return n > 0 && (n & (n - 1)) == 0;
}

otlp_status_t
mpsc_queue_init(struct mpsc_queue *q, size_t capacity)
{
	size_t i;

	if (!q || !is_pow2(capacity))
		return OTLP_ERR_INVALID_ARGUMENT;

	q->slots = calloc(capacity, sizeof(*q->slots));
	if (!q->slots)
		return OTLP_ERR_NOMEM;
	q->mask = capacity - 1;
	atomic_store_explicit(&q->head, 0, memory_order_relaxed);
	atomic_store_explicit(&q->tail, 0, memory_order_relaxed);
	for (i = 0; i < capacity; i++)
		atomic_store_explicit(
			&q->slots[i].seq, i + 1, memory_order_relaxed);
	return OTLP_OK;
}

void
mpsc_queue_free(struct mpsc_queue *q)
{
	if (!q)
		return;
	free(q->slots);
	q->slots = NULL;
	q->mask = 0;
}

otlp_status_t
mpsc_queue_push(struct mpsc_queue *q, void *item)
{
	uint64_t h;
	struct mpsc_slot *slot;
	uint64_t seq;

	h = atomic_load_explicit(&q->head, memory_order_relaxed);
	for (;;)
	{
		slot = &q->slots[h & q->mask];
		seq = atomic_load_explicit(&slot->seq, memory_order_acquire);
		const int64_t diff = (int64_t) seq - (int64_t) (h + 1);
		if (diff == 0)
		{
			/* Slot is empty for this turn — try to claim. */
			if (atomic_compare_exchange_weak_explicit(&q->head,
				    &h,
				    h + 1,
				    memory_order_relaxed,
				    memory_order_relaxed))
				break;
			/* CAS failed: h was reloaded by CAS; retry. */
		}
		else if (diff < 0)
		{
			/* Slot hasn't been released by the consumer yet
			 * → queue is full from this producer's view. */
			return OTLP_ERR_BUFFER_FULL;
		}
		else
		{
			/* Another producer claimed this slot; reload
			 * head and retry. */
			h = atomic_load_explicit(
				&q->head, memory_order_relaxed);
		}
	}

	/* Claimed. Write data, then publish the slot. */
	slot->data = item;
	atomic_store_explicit(
		&slot->seq, h + 1 + q->mask + 1, memory_order_release);
	return OTLP_OK;
}

void *
mpsc_queue_pop(struct mpsc_queue *q)
{
	uint64_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);
	struct mpsc_slot *slot = &q->slots[t & q->mask];
	uint64_t seq = atomic_load_explicit(&slot->seq, memory_order_acquire);
	const int64_t diff = (int64_t) seq - (int64_t) (t + 1 + q->mask + 1);

	if (diff != 0)
		return NULL; /* empty or not yet published */

	void *data = slot->data;
	/* Mark the slot empty for the next turn at this index. */
	atomic_store_explicit(
		&slot->seq, t + q->mask + 2, memory_order_release);
	atomic_store_explicit(&q->tail, t + 1, memory_order_relaxed);
	return data;
}

size_t
mpsc_queue_size(const struct mpsc_queue *q)
{
	uint64_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
	uint64_t t = atomic_load_explicit(&q->tail, memory_order_relaxed);

	return h >= t ? (size_t) (h - t) : 0;
}
