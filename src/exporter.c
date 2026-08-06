/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Batching OTLP/HTTP exporter — caller-tick model.
 *
 * emit() (any thread): clones the span and pushes the pointer into
 * a lock-free MPSC queue. Returns immediately.
 *
 * tick() (single thread): drains the queue, accumulates spans into
 * a pending batch, and either starts a new HTTP POST (encode once)
 * or steps the in-flight request. On terminal HTTP/network status,
 * updates stats and either retries (with backoff) or drops.
 *
 * flush() (single thread): loops tick() until the queue is empty
 * and no request is in flight, or until max_retries is exhausted.
 *
 * shutdown() (any thread): sets an atomic flag; subsequent emit()
 * calls return OTLP_ERR_SHUTDOWN.
 *
 * The library never spawns a thread, never takes a mutex. All
 * cross-thread data flow uses atomics + the MPSC queue.
 */
#include <otlp-c/exporter.h>
#include <otlp-c/span.h>
#include <otlp-c/version.h>

#include "exporter_otel.h"
#include "http_client.h"
#include "internal_util.h"
#include "mpsc_queue.h"
#include "platform.h"
#include "span_internal.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OTLP_DEFAULT_ENDPOINT "http://localhost:4318/v1/traces"
#define OTLP_DEFAULT_BATCH_SIZE 512
#define OTLP_DEFAULT_BATCH_MS 100
#define OTLP_DEFAULT_MAX_RETRIES 5
#define OTLP_DEFAULT_BACKOFF_INIT_MS 1000
#define OTLP_DEFAULT_BACKOFF_MAX_MS 30000
#define OTLP_DEFAULT_CONNECT_TIMEOUT 5000
#define OTLP_DEFAULT_READ_TIMEOUT 10000
#define OTLP_DEFAULT_QUEUE_CAP 4096

struct otlp_exporter
{
	/* Immutable after create. */
	struct otlp_http_url url;
	char *user_agent;
	char *service_name;
	size_t batch_size;
	uint32_t batch_ms;
	uint32_t max_retries;
	uint32_t backoff_initial_ms;
	uint32_t backoff_max_ms;

	/* MPSC queue of pending otlp_span_t*. */
	struct mpsc_queue queue;

	/* Atomic stats + state. */
	_Atomic uint64_t emitted;
	_Atomic uint64_t dropped_full;
	_Atomic uint64_t dropped_err;
	_Atomic uint64_t sent;
	_Atomic uint64_t http_2xx;
	_Atomic uint64_t http_4xx;
	_Atomic uint64_t http_5xx;
	_Atomic uint64_t network_err;
	_Atomic int shutdown_requested;

	/* Tick-private state (no synchronisation needed). */
	otlp_span_t **pending;
	size_t pending_cap;
	size_t pending_count;
	bool first_pending_set;
	uint64_t first_pending_mono;

	otlp_http_request_t *in_flight;
	size_t in_flight_count; /* spans in in_flight batch */
	uint32_t attempt;
	uint64_t backoff_deadline_mono;
	bool backoff_armed;
};

/* ── Helpers ──────────────────────────────────────────────────── */

static uint64_t
now_mono_ms(void)
{
	uint64_t n;

	return (otlp_platform_now_mono_nano(&n) == OTLP_OK) ? n / 1000000ULL
							    : 0;
}

static size_t
round_up_pow2(size_t v)
{
	size_t r = 1;

	while (r < v)
	{
		if (r > (SIZE_MAX / 2))
			return r;
		r *= 2;
	}
	return r;
}

static void
normalize_opts(otlp_exporter_opts_t *o)
{
	if (!o->endpoint)
		o->endpoint = OTLP_DEFAULT_ENDPOINT;
	if (!o->service_name)
		o->service_name = "";
	if (o->batch_size == 0)
		o->batch_size = OTLP_DEFAULT_BATCH_SIZE;
	if (o->batch_ms == 0)
		o->batch_ms = OTLP_DEFAULT_BATCH_MS;
	if (o->max_retries == 0)
		o->max_retries = OTLP_DEFAULT_MAX_RETRIES;
	if (o->backoff_initial_ms == 0)
		o->backoff_initial_ms = OTLP_DEFAULT_BACKOFF_INIT_MS;
	if (o->backoff_max_ms == 0)
		o->backoff_max_ms = OTLP_DEFAULT_BACKOFF_MAX_MS;
	if (o->connect_timeout_ms == 0)
		o->connect_timeout_ms = OTLP_DEFAULT_CONNECT_TIMEOUT;
	if (o->read_timeout_ms == 0)
		o->read_timeout_ms = OTLP_DEFAULT_READ_TIMEOUT;
	if (!o->user_agent)
		o->user_agent = "otlp-c/" OTLP_C_VERSION_STRING;
	if (o->queue_capacity == 0)
		o->queue_capacity = OTLP_DEFAULT_QUEUE_CAP;
	o->queue_capacity = round_up_pow2(o->queue_capacity);
}

static void
free_pending_batch(otlp_span_t **pending, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		otlp_span_free(pending[i]);
}

/* ── Lifecycle ────────────────────────────────────────────────── */

otlp_exporter_t *
otlp_exporter_create(const otlp_exporter_opts_t *opts_in)
{
	struct otlp_exporter *e;
	otlp_exporter_opts_t o;
	otlp_status_t st;

	if (!opts_in)
		return NULL;

	o = *opts_in;
	normalize_opts(&o);

	e = otlp_calloc(1, sizeof(*e));
	if (!e)
		return NULL;

	st = otlp_http_parse_url(o.endpoint, &e->url);
	if (st != OTLP_OK)
		goto fail;

	e->user_agent = otlp_dup_str(o.user_agent);
	e->service_name = otlp_dup_str(o.service_name);
	if (!e->user_agent || !e->service_name)
		goto fail;
	e->batch_size = o.batch_size;
	e->batch_ms = o.batch_ms;
	e->max_retries = o.max_retries;
	e->backoff_initial_ms = o.backoff_initial_ms;
	e->backoff_max_ms = o.backoff_max_ms;

	e->pending_cap = e->batch_size * 2;
	e->pending = otlp_malloc(e->pending_cap * sizeof(*e->pending));
	if (!e->pending)
		goto fail;

	st = mpsc_queue_init(&e->queue, o.queue_capacity);
	if (st != OTLP_OK)
		goto fail;

	atomic_store_explicit(&e->shutdown_requested, 0, memory_order_release);
	return e;

fail:
	otlp_free(e->user_agent);
	otlp_free(e->service_name);
	otlp_free(e->pending);
	otlp_free(e);
	return NULL;
}

void
otlp_exporter_free(otlp_exporter_t *e)
{
	otlp_span_t *span;

	if (!e)
		return;
	/* Drain the queue, freeing any un-sent spans. */
	while ((span = mpsc_queue_pop(&e->queue)) != NULL)
		otlp_span_free(span);
	/* Free pending batch. */
	free_pending_batch(e->pending, e->pending_count);
	/* Free in-flight request. */
	if (e->in_flight)
		otlp_http_request_free(e->in_flight);
	mpsc_queue_free(&e->queue);
	otlp_free(e->pending);
	otlp_free(e->user_agent);
	otlp_free(e->service_name);
	otlp_free(e);
}

/* ── emit (any thread) ────────────────────────────────────────── */

otlp_status_t
otlp_exporter_emit(otlp_exporter_t *e, const otlp_span_t *span)
{
	otlp_span_t *clone;
	otlp_status_t st;

	if (!e || !span)
		return OTLP_ERR_NULL;
	if (atomic_load_explicit(&e->shutdown_requested, memory_order_acquire))
		return OTLP_ERR_SHUTDOWN;

	clone = otlp_span_clone(span);
	if (!clone)
		return OTLP_ERR_NOMEM;
	st = mpsc_queue_push(&e->queue, clone);
	if (st != OTLP_OK)
	{
		otlp_span_free(clone);
		atomic_fetch_add_explicit(
			&e->dropped_full, 1, memory_order_relaxed);
		return st;
	}
	atomic_fetch_add_explicit(&e->emitted, 1, memory_order_relaxed);
	return OTLP_OK;
}

otlp_status_t
otlp_exporter_emit_move(otlp_exporter_t *e, otlp_span_t *span)
{
	otlp_status_t st;

	if (!e || !span)
		return OTLP_ERR_NULL;
	if (atomic_load_explicit(&e->shutdown_requested, memory_order_acquire))
		return OTLP_ERR_SHUTDOWN;

	st = mpsc_queue_push(&e->queue, span);
	if (st != OTLP_OK)
	{
		otlp_span_free(span);
		atomic_fetch_add_explicit(
			&e->dropped_full, 1, memory_order_relaxed);
		return st;
	}
	atomic_fetch_add_explicit(&e->emitted, 1, memory_order_relaxed);
	return OTLP_OK;
}

/* ── tick (single thread) ─────────────────────────────────────── */

static otlp_status_t
try_start_post(struct otlp_exporter *e)
{
	otlp_status_t st;

	if (e->in_flight || e->pending_count == 0)
		return OTLP_OK;
	st = otlp_exporter_otel_build_request(&e->url,
		e->user_agent,
		e->service_name,
		(const otlp_span_t *const *) e->pending,
		e->pending_count,
		&e->in_flight);
	if (st != OTLP_OK)
		return st;
	e->in_flight_count = e->pending_count;
	/* IMPORTANT: do NOT free the pending batch here. It must stay
	 * alive until the in-flight request completes successfully or
	 * is permanently dropped, so retry can re-encode it. The batch
	 * is freed in record_outcome on success / permanent-failure
	 * paths. */
	e->first_pending_set = false;
	return OTLP_OK;
}

static void
record_outcome(struct otlp_exporter *e, int http_status)
{
	if (http_status == 0)
	{
		/* Network-level failure (no HTTP response received).
		 * Treat as transient — same retry path as 5xx. */
		atomic_fetch_add_explicit(
			&e->network_err, 1, memory_order_relaxed);
		e->attempt++;
		if (e->attempt > e->max_retries)
		{
			atomic_fetch_add_explicit(&e->dropped_err,
				e->in_flight_count,
				memory_order_relaxed);
			free_pending_batch(e->pending, e->pending_count);
			e->pending_count = 0;
			e->attempt = 0;
			e->backoff_armed = false;
			return;
		}
		{
			uint32_t delay = e->backoff_initial_ms
				<< (e->attempt - 1);
			if (delay > e->backoff_max_ms ||
				delay < e->backoff_initial_ms)
				delay = e->backoff_max_ms;
			e->backoff_deadline_mono = now_mono_ms() + delay;
			e->backoff_armed = true;
		}
		return;
	}
	if (http_status >= 200 && http_status < 300)
	{
		atomic_fetch_add_explicit(
			&e->http_2xx, 1, memory_order_relaxed);
		atomic_fetch_add_explicit(
			&e->sent, e->in_flight_count, memory_order_relaxed);
		/* Success — free the pending batch (kept across retries). */
		free_pending_batch(e->pending, e->pending_count);
		e->pending_count = 0;
		e->attempt = 0;
		e->backoff_armed = false;
		return;
	}
	if (http_status == 429 || (http_status >= 500 && http_status < 600))
	{
		atomic_fetch_add_explicit(
			&e->http_5xx, 1, memory_order_relaxed);
		e->attempt++;
		if (e->attempt > e->max_retries)
		{
			atomic_fetch_add_explicit(&e->dropped_err,
				e->in_flight_count,
				memory_order_relaxed);
			/* Permanent failure — free the pending batch. */
			free_pending_batch(e->pending, e->pending_count);
			e->pending_count = 0;
			e->attempt = 0;
			e->backoff_armed = false;
		}
		else
		{
			uint32_t delay = e->backoff_initial_ms
				<< (e->attempt - 1);
			if (delay > e->backoff_max_ms ||
				delay < e->backoff_initial_ms)
				delay = e->backoff_max_ms;
			e->backoff_deadline_mono = now_mono_ms() + delay;
			e->backoff_armed = true;
		}
		return;
	}
	/* Permanent 4xx (non-429). */
	atomic_fetch_add_explicit(&e->http_4xx, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(
		&e->dropped_err, e->in_flight_count, memory_order_relaxed);
	/* Permanent failure — free the pending batch. */
	free_pending_batch(e->pending, e->pending_count);
	e->pending_count = 0;
	e->attempt = 0;
	e->backoff_armed = false;
}

otlp_status_t
otlp_exporter_tick(struct otlp_exporter *e, uint32_t max_wait_ms)
{
	uint64_t deadline;
	bool work_done;

	if (!e)
		return OTLP_ERR_NULL;
	deadline = now_mono_ms() + max_wait_ms;

	do
	{
		work_done = false;

		/* 1. Drain queue into pending. */
		while (e->pending_count < e->pending_cap)
		{
			otlp_span_t *s = mpsc_queue_pop(&e->queue);

			if (!s)
				break;
			e->pending[e->pending_count++] = s;
			if (!e->first_pending_set)
			{
				e->first_pending_mono = now_mono_ms();
				e->first_pending_set = true;
			}
			work_done = true;
		}

		/* 2. Start POST if batch ready. */
		if (!e->in_flight && !e->backoff_armed &&
			(e->pending_count >= e->batch_size ||
				(e->first_pending_set &&
					now_mono_ms() - e->first_pending_mono >=
						e->batch_ms) ||
				(atomic_load_explicit(&e->shutdown_requested,
					 memory_order_relaxed) &&
					e->pending_count > 0)))
		{
			otlp_status_t st = try_start_post(e);
			if (st == OTLP_OK)
				work_done = true;
		}

		/* 3. Step in-flight request. */
		if (e->in_flight)
		{
			otlp_status_t st = otlp_http_request_step(e->in_flight);
			otlp_http_req_state_t s =
				otlp_http_request_state(e->in_flight);

			if (s == OTLP_HTTP_REQ_DONE ||
				s == OTLP_HTTP_REQ_FAILED)
			{
				int status = (s == OTLP_HTTP_REQ_DONE)
					? otlp_http_request_http_status(
						  e->in_flight)
					: 0;
				record_outcome(e, status);
				otlp_http_request_free(e->in_flight);
				e->in_flight = NULL;
				e->in_flight_count = 0;
			}
			work_done = true;
			(void) st;
		}

		/* 4. Backoff timer. The pending batch is retained
		 * across retries (freed in record_outcome on success
		 * or permanent failure); re-encode + retry now. */
		if (e->backoff_armed && !e->in_flight &&
			now_mono_ms() >= e->backoff_deadline_mono)
		{
			e->backoff_armed = false;
			if (try_start_post(e) == OTLP_OK && e->in_flight)
				work_done = true;
		}

		/* 5. If nothing else to do but we're waiting on backoff,
		 * sleep briefly so the tick actually advances time toward
		 * the deadline instead of returning immediately. */
		if (!work_done && e->backoff_armed && !e->in_flight)
		{
			struct timespec ts = { 0, 1 * 1000 * 1000 /* 1ms */ };
			nanosleep(&ts, NULL);
			work_done = true;  /* keep the loop alive */
		}

		if (!work_done)
			break;
	} while (now_mono_ms() < deadline);

	return OTLP_OK;
}

otlp_status_t
otlp_exporter_flush(otlp_exporter_t *e)
{
	uint64_t deadline;
	uint64_t now;

	if (!e)
		return OTLP_ERR_NULL;
	deadline = now_mono_ms() + 30000; /* hard cap: 30s */

	do
	{
		otlp_exporter_tick(e, 100);
		now = now_mono_ms();
	} while ((e->pending_count > 0 || e->in_flight ||
			 mpsc_queue_size(&e->queue) > 0) &&
		now < deadline);

	if (e->pending_count > 0 || e->in_flight)
		return OTLP_ERR_NETWORK;
	return OTLP_OK;
}

otlp_status_t
otlp_exporter_shutdown(otlp_exporter_t *e)
{
	if (!e)
		return OTLP_ERR_NULL;
	atomic_store_explicit(&e->shutdown_requested, 1, memory_order_release);
	return OTLP_OK;
}

otlp_status_t
otlp_exporter_poll_fds(otlp_exporter_t *e,
	otlp_poll_fd_t *out,
	size_t cap,
	size_t *n_out)
{
	if (!e || !n_out)
		return OTLP_ERR_NULL;
	if (!e->in_flight || cap == 0)
	{
		*n_out = 0;
		return OTLP_OK;
	}
	if (!out)
	{
		*n_out = 0;
		return OTLP_ERR_NULL;
	}
	out[0].fd = otlp_http_request_fd(e->in_flight);
	out[0].events = otlp_http_request_events(e->in_flight);
	*n_out = 1;
	return OTLP_OK;
}

otlp_status_t
otlp_exporter_get_stats(otlp_exporter_t *e, otlp_exporter_stats_t *out)
{
	if (!e || !out)
		return OTLP_ERR_NULL;
	out->emitted = atomic_load_explicit(&e->emitted, memory_order_relaxed);
	out->dropped_full =
		atomic_load_explicit(&e->dropped_full, memory_order_relaxed);
	out->dropped_err =
		atomic_load_explicit(&e->dropped_err, memory_order_relaxed);
	out->sent = atomic_load_explicit(&e->sent, memory_order_relaxed);
	out->http_2xx =
		atomic_load_explicit(&e->http_2xx, memory_order_relaxed);
	out->http_4xx =
		atomic_load_explicit(&e->http_4xx, memory_order_relaxed);
	out->http_5xx =
		atomic_load_explicit(&e->http_5xx, memory_order_relaxed);
	out->network_err =
		atomic_load_explicit(&e->network_err, memory_order_relaxed);
	return OTLP_OK;
}
