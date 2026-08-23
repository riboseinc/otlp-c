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

#include "exporter_internal.h"
#include "exporter_otel.h"
#include "http_client.h"
#include "internal_util.h"
#include "log_internal.h"
#include "metric_internal.h"
#include "mpsc_queue.h"
#include "otlp_messages.h"
#include "platform.h"
#include "span_internal.h"

#include "atomic_compat.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define OTLP_DEFAULT_ENDPOINT "http://localhost:4318/v1/traces"
#define OTLP_DEFAULT_BATCH_SIZE 512
/* Upper clamp for caller-supplied batch_size. Prevents
 * batch_size * 2 * sizeof(ptr) from overflowing size_t in the
 * pending-array allocation. 1M items per batch is already
 * enormous (~200 MB of encoded wire data); callers wanting
 * more should shard across multiple exporters. */
#define OTLP_MAX_BATCH_SIZE (1024u * 1024u)
#define OTLP_DEFAULT_BATCH_MS 100
#define OTLP_DEFAULT_MAX_RETRIES 5
#define OTLP_RESOURCE_MAX_ATTRS 128
#define OTLP_DEFAULT_BACKOFF_INIT_MS 1000
#define OTLP_DEFAULT_BACKOFF_MAX_MS 30000
#define OTLP_DEFAULT_CONNECT_TIMEOUT 5000
#define OTLP_DEFAULT_READ_TIMEOUT 10000
#define OTLP_DEFAULT_FLUSH_TIMEOUT_MS 30000
#define OTLP_DEFAULT_QUEUE_CAP 4096

struct otlp_exporter
{
	/* Immutable after create. */
	struct otlp_http_url url;
	char *user_agent;
	char *service_name;
	struct otlp_attr_vec resource_attrs;
	size_t batch_size;
	uint32_t batch_ms;
	uint32_t max_retries;
	uint32_t backoff_initial_ms;
	uint32_t backoff_max_ms;
	uint32_t flush_timeout_ms;
	uint32_t connect_timeout_ms;
	uint32_t read_timeout_ms;

	/* MPSC queue of pending otlp_span_t*. */
	struct mpsc_queue queue;

	/* MPSC queues for async metric/log (v0.5.28). */
	struct mpsc_queue metric_queue;
	struct mpsc_queue log_queue;

	/* Atomic stats + state. */
	otlp_atomic_u64 emitted;
	otlp_atomic_u64 dropped_full;
	otlp_atomic_u64 dropped_err;
	otlp_atomic_u64 sent;
	otlp_atomic_u64 http_2xx;
	otlp_atomic_u64 http_4xx;
	otlp_atomic_u64 http_5xx;
	otlp_atomic_u64 network_err;
	otlp_atomic_u64 emitted_metrics;
	otlp_atomic_u64 sent_metrics;
	otlp_atomic_u64 dropped_metrics_full;
	otlp_atomic_u64 dropped_metrics_err;
	otlp_atomic_u64 emitted_logs;
	otlp_atomic_u64 sent_logs;
	otlp_atomic_u64 dropped_logs_full;
	otlp_atomic_u64 dropped_logs_err;
	otlp_atomic_u64 rejected_spans;
	otlp_atomic_u64 rejected_metrics;
	otlp_atomic_u64 rejected_logs;
	otlp_atomic_int shutdown_requested;

	/* Optional diagnostic callbacks (NULL = no-op). The event
	 * callback receives the structured model; the string logger
	 * receives the message derived from it by format_event(). */
	otlp_log_fn log_fn;
	void *log_ctx;
	otlp_event_fn event_fn;
	void *event_ctx;

	/* Tick-private state (no synchronisation needed). */
	otlp_span_t **pending;
	size_t pending_cap;
	size_t pending_count;
	bool first_pending_set;
	uint64_t first_pending_mono;

	/* Metric/log pending batches (tick-private). */
	otlp_metric_t **metric_pending;
	size_t metric_pending_cap;
	size_t metric_pending_count;
	bool metric_first_set;
	uint64_t metric_first_mono;
	otlp_log_record_t **log_pending;
	size_t log_pending_cap;
	size_t log_pending_count;
	bool log_first_set;
	uint64_t log_first_mono;

	/* In-flight request state. in_flight_signal identifies which
	 * signal's batch is being POSTed (0=span, 1=metric, 2=log). */
	otlp_http_request_t *in_flight;
	int in_flight_signal;
	size_t in_flight_count;
	uint32_t attempt;
	uint64_t backoff_deadline_mono;
	bool backoff_armed;
	/* Cached TCP connection for HTTP keep-alive. Owned by the exporter,
	 * donated to the next in_flight request, re-acquired on success. */
	otlp_socket_t *keepalive_sock;
	bool null_transport;
	otlp_null_transport_status_fn null_transport_status_fn;
	void *null_transport_status_ctx;
	/* Backoff-jitter PRNG (xorshift64s). Tick-thread-only: read
	 * and written exclusively from the tick caller, so plain
	 * non-atomic state is correct. */
	uint64_t jitter_prng;
};

/* ── Helpers ──────────────────────────────────────────────────── */

static uint64_t
now_mono_ms(void)
{
	uint64_t n;

	return (otlp_platform_now_mono_nano(&n) == OTLP_OK) ? n / 1000000ULL
							    : 0;
}

/* xorshift64s — state must be non-zero. */
static uint64_t
jitter_next(uint64_t *s)
{
	uint64_t v = *s;

	v ^= v << 13;
	v ^= v >> 7;
	v ^= v << 17;
	*s = v;
	return v * 0x2545F4914F6CDD1DULL;
}

/* Retry delay for `attempt` (1-based): exponential backoff with
 * FULL jitter — uniform in [0, min(initial << (attempt-1), max)].
 * The shift is computed in uint64_t with the shift count clamped
 * (a caller-set max_retries above ~33 would otherwise shift a
 * uint32 by >= its width: undefined behavior). One helper for
 * every retry path (DRY: the network-error and 429/5xx paths
 * previously duplicated this computation). */
static uint32_t
backoff_delay_ms(const struct otlp_exporter *e, uint32_t attempt)
{
	uint64_t exp;
	uint32_t shift = attempt - 1;
	uint64_t delay;

	if (shift > 31)
		shift = 31; /* beyond any uint32 magnitude anyway */
	exp = (uint64_t) e->backoff_initial_ms << shift;
	delay = exp > e->backoff_max_ms ? e->backoff_max_ms : exp;
	if (delay == 0)
		return 0;
	return (uint32_t)(
		jitter_next((uint64_t *) &e->jitter_prng) % (delay + 1));
}

static const char *
signal_name(otlp_signal_id_t s)
{
	switch (s)
	{
		case OTLP_SIGNAL_METRICS:
			return "metrics";
		case OTLP_SIGNAL_LOGS:
			return "logs";
		default:
			return "spans";
	}
}

/* Render the event's human-readable line. THE single presentation
 * point: every diagnostic string is derived from the otlp_event_t
 * model here, so the structured and string views cannot diverge. */
static void
format_event(char *buf, size_t cap, const otlp_event_t *ev)
{
	switch (ev->code)
	{
		case OTLP_EVT_QUEUE_FULL:
			snprintf(buf,
				cap,
				"%s dropped: queue full",
				signal_name(ev->signal));
			break;
		case OTLP_EVT_BATCH_SENT:
			snprintf(buf,
				cap,
				"batch sent: %llu %s",
				(unsigned long long) ev->count,
				signal_name(ev->signal));
			break;
		case OTLP_EVT_RETRY_ARMED:
			if (ev->http_status == 0)
				snprintf(buf,
					cap,
					"network error; %s retry %u/%u in %ums",
					signal_name(ev->signal),
					ev->attempt,
					ev->max_retries,
					ev->delay_ms);
			else
				snprintf(buf,
					cap,
					"%s HTTP %d; retry %u/%u in %ums%s",
					signal_name(ev->signal),
					ev->http_status,
					ev->attempt,
					ev->max_retries,
					ev->delay_ms,
					ev->server_driven
						? " (server Retry-After)"
						: "");
			break;
		case OTLP_EVT_ITEMS_DROPPED:
			if (ev->drop_reason == OTLP_DROP_HTTP_STATUS)
				snprintf(buf,
					cap,
					"HTTP %d: %llu %s dropped (permanent)",
					ev->http_status,
					(unsigned long long) ev->count,
					signal_name(ev->signal));
			else if (ev->http_status == 0)
				snprintf(buf,
					cap,
					"network error: %llu %s dropped "
					"(max retries %u)",
					(unsigned long long) ev->count,
					signal_name(ev->signal),
					ev->max_retries);
			else
				snprintf(buf,
					cap,
					"HTTP %d: %llu %s dropped "
					"(max retries %u)",
					ev->http_status,
					(unsigned long long) ev->count,
					signal_name(ev->signal),
					ev->max_retries);
			break;
		case OTLP_EVT_PARTIAL_SUCCESS:
			if (ev->count > 0)
				snprintf(buf,
					cap,
					"collector partial success: %llu of "
					"%llu "
					"%s rejected",
					(unsigned long long) ev->rejected,
					(unsigned long long) ev->count,
					signal_name(ev->signal));
			else
				snprintf(buf,
					cap,
					"collector partial success: %llu %s "
					"rejected",
					(unsigned long long) ev->rejected,
					signal_name(ev->signal));
			if (ev->detail_len > 0 && ev->detail)
				snprintf(buf + strlen(buf),
					cap - strlen(buf),
					": %.*s",
					(int) (ev->detail_len > 128
							? 128
							: ev->detail_len),
					ev->detail);
			break;
		case OTLP_EVT_SYNC_FLUSH_FAILED:
			if (ev->status == OTLP_ERR_TIMEOUT)
				snprintf(buf,
					cap,
					"sync flush %s: timeout after %ums",
					signal_name(ev->signal),
					ev->timeout_ms);
			else if (ev->http_status > 0)
				snprintf(buf,
					cap,
					"sync flush %s: HTTP %d",
					signal_name(ev->signal),
					ev->http_status);
			else
				snprintf(buf,
					cap,
					"sync flush %s: failed (st=%d)",
					signal_name(ev->signal),
					(int) ev->status);
			break;
		default:
			snprintf(buf, cap, "event %d", (int) ev->code);
			break;
	}
}

/* Dispatch one diagnostic: structured callback first, then the
 * string logger (if installed) with the message derived from the
 * same event. No-ops when neither callback is set — zero overhead
 * beyond building the struct. */
static void
event_log(const struct otlp_exporter *e, const otlp_event_t *ev)
{
	if (!e)
		return;
	if (e->event_fn)
		e->event_fn(e->event_ctx, ev);
	if (e->log_fn)
	{
		char buf[256];

		format_event(buf, sizeof(buf), ev);
		e->log_fn(e->log_ctx, ev->level, buf);
	}
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
	if (o->batch_size > OTLP_MAX_BATCH_SIZE)
		o->batch_size = OTLP_MAX_BATCH_SIZE;
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
	if (o->flush_timeout_ms == 0)
		o->flush_timeout_ms = OTLP_DEFAULT_FLUSH_TIMEOUT_MS;
	if (!o->user_agent)
		o->user_agent = "otlp-c/" OTLP_C_VERSION_STRING;
	if (o->queue_capacity == 0)
		o->queue_capacity = OTLP_DEFAULT_QUEUE_CAP;
	o->queue_capacity = round_up_pow2(o->queue_capacity);
}

/* ── Lifecycle ────────────────────────────────────────────────── */

/* Type-erased free / clone wrappers. Each signal's typed function
 * is wrapped in a void*-signature helper so it can be stored in a
 * signal-path descriptor and dispatched uniformly. Defined early
 * so all sections (lifecycle, emit, record_outcome, start_post)
 * can reference them. */
static void
span_free_void(void *p)
{
	otlp_span_free(p);
}

static void
metric_free_void(void *p)
{
	otlp_metric_free(p);
}

static void
log_free_void(void *p)
{
	otlp_log_record_free(p);
}

/* Per-signal descriptor for the free-path drain. Bundles the queue
 * (to pop un-sent items from) with the pending array (to free
 * batched-but-not-yet-POSTed items) and the type-erased free fn.
 *
 * Used by otlp_exporter_free to drain all three signals in one loop
 * instead of triplicating the drain+free_pending pair. */
struct signal_drain_path
{
	struct mpsc_queue *queue;
	void **pending;
	size_t pending_count;
	void (*free_item)(void *);
};

/* Drain one signal: pop everything from the queue and free it,
 * then free the pending batch. */
static void
drain_signal(const struct signal_drain_path *p)
{
	void *item;
	size_t i;

	while ((item = mpsc_queue_pop(p->queue)) != NULL)
		p->free_item(item);
	for (i = 0; i < p->pending_count; i++)
		p->free_item(p->pending[i]);
}

const struct otlp_attribute *
otlp_exporter_get_resource_attrs(const otlp_exporter_t *e, size_t *n)
{
	if (n)
		*n = e ? e->resource_attrs.n : 0;
	return e ? e->resource_attrs.items : NULL;
}

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

	if (o.service_name && !otlp_str_is_utf8(o.service_name))
		goto fail;
	e->user_agent = otlp_dup_str(o.user_agent);
	e->service_name = otlp_dup_str(o.service_name);
	if (!e->user_agent || !e->service_name)
		goto fail;
	if (o.n_resource_attributes > 0 && o.resource_attributes)
	{
		size_t i;
		bool svc_set = o.service_name && o.service_name[0];

		/* Resource attributes on the ONE model (v0.5.92): the
		 * set-attribute engine gives map semantics (duplicate
		 * keys collapse last-write-wins), deep copy, and all
		 * seven AnyValue types. "service.name" yields to the
		 * dedicated opt (v0.5.78). Empty keys and empty string
		 * values are skipped (pre-v0.5.92 behavior preserved). */
		for (i = 0; i < o.n_resource_attributes; i++)
		{
			const otlp_resource_attr_t *src =
				&o.resource_attributes[i];

			if (!src->key || !src->key[0])
				continue;
			if (svc_set && strcmp(src->key, "service.name") == 0)
				continue;
			if (src->value.type == OTLP_VALUE_STRING &&
				(!src->value.v.string_val ||
					!src->value.v.string_val[0]))
				continue;
			{
				otlp_status_t rst =
					otlp_attr_vec_set(&e->resource_attrs,
						OTLP_RESOURCE_MAX_ATTRS,
						src->key,
						&src->value);

				if (rst != OTLP_OK)
					goto fail;
			}
		}
	}

	e->batch_size = o.batch_size;
	e->batch_ms = o.batch_ms;
	e->max_retries = o.max_retries;
	e->backoff_initial_ms = o.backoff_initial_ms;
	e->backoff_max_ms = o.backoff_max_ms;
	e->flush_timeout_ms = o.flush_timeout_ms;
	e->connect_timeout_ms = o.connect_timeout_ms;
	e->read_timeout_ms = o.read_timeout_ms;

	e->pending_cap = e->batch_size * 2;
	e->pending = otlp_malloc(e->pending_cap * sizeof(*e->pending));
	if (!e->pending)
		goto fail;

	e->metric_pending_cap = e->batch_size * 2;
	e->metric_pending =
		otlp_malloc(e->metric_pending_cap * sizeof(*e->metric_pending));
	if (!e->metric_pending)
		goto fail;

	e->log_pending_cap = e->batch_size * 2;
	e->log_pending =
		otlp_malloc(e->log_pending_cap * sizeof(*e->log_pending));
	if (!e->log_pending)
		goto fail;

	st = mpsc_queue_init(&e->queue, o.queue_capacity);
	if (st != OTLP_OK)
		goto fail;
	st = mpsc_queue_init(&e->metric_queue, o.queue_capacity);
	if (st != OTLP_OK)
		goto fail;
	st = mpsc_queue_init(&e->log_queue, o.queue_capacity);
	if (st != OTLP_OK)
		goto fail;

	e->in_flight_signal = 0; /* SPAN */
	{
		uint64_t seed = 0;

		(void) otlp_platform_now_mono_nano(&seed);
		seed ^= (uint64_t)(uintptr_t) e;
		if (seed == 0)
			seed = 0x9E3779B97F4A7C15ULL;
		e->jitter_prng = seed;
	}
	otlp_atomic_store_int(
		&e->shutdown_requested, 0, OTLP_MEMORY_ORDER_RELEASE);
	return e;

fail:
	otlp_free(e->user_agent);
	otlp_free(e->service_name);
	otlp_attr_vec_free(&e->resource_attrs);
	otlp_free(e->pending);
	otlp_free(e->metric_pending);
	otlp_free(e->log_pending);
	/* mpsc_queue_free is safe on uninitialized queues (slots=NULL
	 * → otlp_free(NULL) is a no-op). If any of the three queue
	 * inits succeeded before the failure, its slots must be freed;
	 * otherwise they'd leak. */
	mpsc_queue_free(&e->queue);
	mpsc_queue_free(&e->metric_queue);
	mpsc_queue_free(&e->log_queue);
	otlp_free(e);
	return NULL;
}

void
otlp_exporter_free(otlp_exporter_t *e)
{
	struct signal_drain_path drains[3];
	size_t i;

	if (!e)
		return;
	/* Drain queues + free pending batches, all signals. The drain
	 * is table-driven: adding a 4th signal is one entry, not a
	 * copy-paste of the while + for pair. */
	drains[0] = (struct signal_drain_path){
		.queue = &e->queue,
		.pending = (void **) e->pending,
		.pending_count = e->pending_count,
		.free_item = span_free_void,
	};
	drains[1] = (struct signal_drain_path){
		.queue = &e->metric_queue,
		.pending = (void **) e->metric_pending,
		.pending_count = e->metric_pending_count,
		.free_item = metric_free_void,
	};
	drains[2] = (struct signal_drain_path){
		.queue = &e->log_queue,
		.pending = (void **) e->log_pending,
		.pending_count = e->log_pending_count,
		.free_item = log_free_void,
	};
	for (i = 0; i < 3; i++)
		drain_signal(&drains[i]);
	/* Free in-flight request. */
	if (e->in_flight)
		otlp_http_request_free(e->in_flight);
	/* Close any cached keep-alive socket. */
	if (e->keepalive_sock)
		otlp_socket_close(e->keepalive_sock);
	mpsc_queue_free(&e->queue);
	mpsc_queue_free(&e->metric_queue);
	mpsc_queue_free(&e->log_queue);
	otlp_free(e->pending);
	otlp_free(e->metric_pending);
	otlp_free(e->log_pending);
	otlp_free(e->user_agent);
	otlp_free(e->service_name);
	otlp_attr_vec_free(&e->resource_attrs);
	otlp_free(e);
}

/* ── emit (any thread) ────────────────────────────────────────── */

/* Type-erased clone wrappers. The free wrappers
 * (span_free_void, metric_free_void, log_free_void) are defined
 * in the Lifecycle section above; they're shared across sections. */
static void *
span_clone_void(const void *p)
{
	return otlp_span_clone((const otlp_span_t *) p);
}

static void *
metric_clone_void(const void *p)
{
	return otlp_metric_clone((const otlp_metric_t *) p);
}

static void *
log_clone_void(const void *p)
{
	return otlp_log_record_clone((const otlp_log_record_t *) p);
}

/* Per-signal descriptor for the emit pipeline. Bundles the queue,
 * counters, and type-erased free/clone hooks so the emit logic can
 * be table-driven. Adding a new signal = a new descriptor + a
 * public wrapper; the core logic doesn't change. */
struct signal_emit_path
{
	struct mpsc_queue *queue;
	otlp_atomic_u64 *emitted_counter;
	otlp_atomic_u64 *dropped_full_counter;
	void (*free_item)(void *);
	void *(*clone_item)(const void *);
	otlp_signal_id_t signal;
};

/* Core of every emit_move variant. NULL + shutdown + push + stats
 * are signal-agnostic; only the descriptor varies. */
static otlp_status_t
emit_move_common(struct otlp_exporter *e,
	const struct signal_emit_path *p,
	void *item)
{
	otlp_status_t st;

	if (!e || !item)
		return OTLP_ERR_NULL;
	if (otlp_atomic_load_int(
		    &e->shutdown_requested, OTLP_MEMORY_ORDER_ACQUIRE))
	{
		/* Honor the move contract: we own the item from call entry.
		 * The docstring promises the library frees on drop. */
		p->free_item(item);
		return OTLP_ERR_SHUTDOWN;
	}

	st = mpsc_queue_push(p->queue, item);
	if (st != OTLP_OK)
	{
		otlp_event_t ev = {
			.code = OTLP_EVT_QUEUE_FULL,
			.level = OTLP_LOG_WARN,
			.signal = p->signal,
			.count = 1,
			.drop_reason = OTLP_DROP_QUEUE_FULL,
		};

		p->free_item(item);
		otlp_atomic_fetch_add_u64(
			p->dropped_full_counter, 1, OTLP_MEMORY_ORDER_RELAXED);
		event_log(e, &ev);
		return st;
	}
	otlp_atomic_fetch_add_u64(
		p->emitted_counter, 1, OTLP_MEMORY_ORDER_RELAXED);
	return OTLP_OK;
}

/* Core of every clone-variant emit. NULL + shutdown checks happen
 * BEFORE the clone (v0.5.42 symmetry), so we never allocate under
 * shutdown contention. The move variant's re-check catches the
 * race between clone and shutdown. */
static otlp_status_t
emit_clone_common(struct otlp_exporter *e,
	const struct signal_emit_path *p,
	const void *item)
{
	void *clone;

	if (!e || !item)
		return OTLP_ERR_NULL;
	if (otlp_atomic_load_int(
		    &e->shutdown_requested, OTLP_MEMORY_ORDER_ACQUIRE))
		return OTLP_ERR_SHUTDOWN;
	clone = p->clone_item(item);
	if (!clone)
		return OTLP_ERR_NOMEM;
	return emit_move_common(e, p, clone);
}

otlp_status_t
otlp_exporter_emit_move(otlp_exporter_t *e, otlp_span_t *span)
{
	struct signal_emit_path p = {
		.queue = &e->queue,
		.emitted_counter = &e->emitted,
		.dropped_full_counter = &e->dropped_full,
		.free_item = span_free_void,
		.clone_item = span_clone_void,
		.signal = OTLP_SIGNAL_TRACES,
	};
	return emit_move_common(e, &p, span);
}

otlp_status_t
otlp_exporter_emit_metric_move(otlp_exporter_t *e, otlp_metric_t *metric)
{
	struct signal_emit_path p = {
		.queue = &e->metric_queue,
		.emitted_counter = &e->emitted_metrics,
		.dropped_full_counter = &e->dropped_metrics_full,
		.free_item = metric_free_void,
		.clone_item = metric_clone_void,
		.signal = OTLP_SIGNAL_METRICS,
	};
	return emit_move_common(e, &p, metric);
}

otlp_status_t
otlp_exporter_emit_log_move(otlp_exporter_t *e, otlp_log_record_t *log)
{
	struct signal_emit_path p = {
		.queue = &e->log_queue,
		.emitted_counter = &e->emitted_logs,
		.dropped_full_counter = &e->dropped_logs_full,
		.free_item = log_free_void,
		.clone_item = log_clone_void,
		.signal = OTLP_SIGNAL_LOGS,
	};
	return emit_move_common(e, &p, log);
}

otlp_status_t
otlp_exporter_emit(otlp_exporter_t *e, const otlp_span_t *span)
{
	struct signal_emit_path p = {
		.queue = &e->queue,
		.emitted_counter = &e->emitted,
		.dropped_full_counter = &e->dropped_full,
		.free_item = span_free_void,
		.clone_item = span_clone_void,
		.signal = OTLP_SIGNAL_TRACES,
	};
	return emit_clone_common(e, &p, span);
}

otlp_status_t
otlp_exporter_emit_metric(otlp_exporter_t *e, const otlp_metric_t *metric)
{
	struct signal_emit_path p = {
		.queue = &e->metric_queue,
		.emitted_counter = &e->emitted_metrics,
		.dropped_full_counter = &e->dropped_metrics_full,
		.free_item = metric_free_void,
		.clone_item = metric_clone_void,
		.signal = OTLP_SIGNAL_METRICS,
	};
	return emit_clone_common(e, &p, metric);
}

otlp_status_t
otlp_exporter_emit_log(otlp_exporter_t *e, const otlp_log_record_t *log)
{
	struct signal_emit_path p = {
		.queue = &e->log_queue,
		.emitted_counter = &e->emitted_logs,
		.dropped_full_counter = &e->dropped_logs_full,
		.free_item = log_free_void,
		.clone_item = log_clone_void,
		.signal = OTLP_SIGNAL_LOGS,
	};
	return emit_clone_common(e, &p, log);
}

/* ── tick (single thread) ─────────────────────────────────────── */

/* Signal kind constants (0=span, 1=metric, 2=log). */
enum
{
	SIGNAL_SPAN = 0,
	SIGNAL_METRIC = 1,
	SIGNAL_LOG = 2
};

/* Table-driven signal descriptor. Bundles the per-signal state
 * (queue, pending array, timer, start_post function) so tick()
 * can iterate over all three signals in one loop instead of
 * triplicating the drain/null-transport/POST-start/backoff logic.
 *
 * All pointer fields point INTO the exporter struct, so mutations
 * through this descriptor update the exporter directly. Built once
 * at the top of tick(); valid for the duration of the call.
 *
 * Adding a new signal = one more paths[] entry (OCP). */
struct signal_path
{
	struct mpsc_queue *queue;
	void **pending;
	size_t pending_cap;
	size_t *pending_count;
	bool *first_set;
	uint64_t *first_mono;
	int signal_kind;
	otlp_status_t (*start_post)(struct otlp_exporter *e);
};

/* Per-signal descriptor for the record_outcome path. Bundles the
 * pending batch (type-erased), the sent/dropped counters, and the
 * signal name so outcome handling is table-driven rather than
 * switch-on-in_flight_signal.
 *
 * Built once at the top of record_outcome; passed by const pointer
 * to the helpers that need it. */
struct signal_record_path
{
	void **pending;
	size_t *pending_count;
	bool *first_set;
	void (*free_item)(void *);
	otlp_atomic_u64 *sent_counter;
	otlp_atomic_u64 *dropped_err_counter;
	otlp_atomic_u64 *rejected_counter;
	otlp_signal_id_t signal;
};

/* Look up the record-path descriptor for the in-flight signal. */
static struct signal_record_path
record_path_for(struct otlp_exporter *e)
{
	switch (e->in_flight_signal)
	{
		case SIGNAL_METRIC:
			return (struct signal_record_path){
				.pending = (void **) e->metric_pending,
				.pending_count = &e->metric_pending_count,
				.first_set = &e->metric_first_set,
				.free_item = metric_free_void,
				.sent_counter = &e->sent_metrics,
				.dropped_err_counter = &e->dropped_metrics_err,
				.rejected_counter = &e->rejected_metrics,
				.signal = OTLP_SIGNAL_METRICS,
			};
		case SIGNAL_LOG:
			return (struct signal_record_path){
				.pending = (void **) e->log_pending,
				.pending_count = &e->log_pending_count,
				.first_set = &e->log_first_set,
				.free_item = log_free_void,
				.sent_counter = &e->sent_logs,
				.dropped_err_counter = &e->dropped_logs_err,
				.rejected_counter = &e->rejected_logs,
				.signal = OTLP_SIGNAL_LOGS,
			};
		case SIGNAL_SPAN:
		default:
			return (struct signal_record_path){
				.pending = (void **) e->pending,
				.pending_count = &e->pending_count,
				.first_set = &e->first_pending_set,
				.free_item = span_free_void,
				.sent_counter = &e->sent,
				.dropped_err_counter = &e->dropped_err,
				.rejected_counter = &e->rejected_spans,
				.signal = OTLP_SIGNAL_TRACES,
			};
	}
}

/* Clear the pending batch for whichever signal is in-flight. */
static void
clear_in_flight_batch(struct otlp_exporter *e,
	const struct signal_record_path *p)
{
	size_t i;
	for (i = 0; i < *p->pending_count; i++)
		p->free_item(p->pending[i]);
	*p->pending_count = 0;
	*p->first_set = false;
	/* Reset retry state for the next batch. */
	e->attempt = 0;
	e->backoff_armed = false;
}

static void
add_sent_for_signal(const struct signal_record_path *p,
	uint64_t in_flight_count)
{
	otlp_atomic_fetch_add_u64(
		p->sent_counter, in_flight_count, OTLP_MEMORY_ORDER_RELAXED);
}

static void
add_dropped_err_for_signal(const struct signal_record_path *p,
	uint64_t in_flight_count)
{
	otlp_atomic_fetch_add_u64(p->dropped_err_counter,
		in_flight_count,
		OTLP_MEMORY_ORDER_RELAXED);
}

/* Type-erased wrappers around the typed otlp_exporter_otel_build_*
 * functions. Each takes const void *const * items so the start-post
 * descriptor can hold a single function-pointer type. The cast is
 * localized here; the typed build helpers retain full type safety. */
static otlp_status_t
build_span_request_void(const struct otlp_http_url *url,
	const char *user_agent,
	const char *service_name,
	const struct otlp_attribute *res_attrs,
	size_t n_res_attrs,
	const void *const *items,
	size_t n_items,
	uint32_t connect_to,
	uint32_t read_to,
	otlp_socket_t *reuse,
	otlp_http_request_t **out)
{
	return otlp_exporter_otel_build_span_request(url,
		user_agent,
		service_name,
		res_attrs,
		n_res_attrs,
		(const otlp_span_t *const *) items,
		n_items,
		connect_to,
		read_to,
		reuse,
		out);
}

static otlp_status_t
build_metric_request_void(const struct otlp_http_url *url,
	const char *user_agent,
	const char *service_name,
	const struct otlp_attribute *res_attrs,
	size_t n_res_attrs,
	const void *const *items,
	size_t n_items,
	uint32_t connect_to,
	uint32_t read_to,
	otlp_socket_t *reuse,
	otlp_http_request_t **out)
{
	return otlp_exporter_otel_build_metric_request(url,
		user_agent,
		service_name,
		res_attrs,
		n_res_attrs,
		(const otlp_metric_t *const *) items,
		n_items,
		connect_to,
		read_to,
		reuse,
		out);
}

static otlp_status_t
build_log_request_void(const struct otlp_http_url *url,
	const char *user_agent,
	const char *service_name,
	const struct otlp_attribute *res_attrs,
	size_t n_res_attrs,
	const void *const *items,
	size_t n_items,
	uint32_t connect_to,
	uint32_t read_to,
	otlp_socket_t *reuse,
	otlp_http_request_t **out)
{
	return otlp_exporter_otel_build_log_request(url,
		user_agent,
		service_name,
		res_attrs,
		n_res_attrs,
		(const otlp_log_record_t *const *) items,
		n_items,
		connect_to,
		read_to,
		reuse,
		out);
}

/* Per-signal descriptor for the start-post path. Bundles the
 * type-erased pending array, count, first_set flag, signal kind,
 * and a build_request fn pointer so the start logic can be
 * table-driven rather than triplicated. */
struct signal_start_path
{
	const void *pending;
	size_t pending_count;
	bool *first_set;
	int signal_kind;
	otlp_status_t (*build_request)(const struct otlp_http_url *,
		const char *,
		const char *,
		const struct otlp_attribute *,
		size_t,
		const void *const *,
		size_t,
		uint32_t,
		uint32_t,
		otlp_socket_t *,
		otlp_http_request_t **);
};

/* Encode + start the HTTP POST for whichever signal's descriptor
 * is passed. Caller pre-builds the descriptor from exporter state.
 * On success: in_flight is set, keepalive_sock is consumed (or
 * cleared), in_flight_signal/count are populated, first_set cleared.
 * On failure: keepalive_sock is cleared (the build path closed it
 * or did not take it). */
static otlp_status_t
try_start_post_common(struct otlp_exporter *e,
	const struct signal_start_path *p)
{
	otlp_status_t st;

	if (e->in_flight || p->pending_count == 0)
		return OTLP_OK;
	st = p->build_request(&e->url,
		e->user_agent,
		e->service_name,
		e->resource_attrs.items,
		e->resource_attrs.n,
		(const void *const *) p->pending,
		p->pending_count,
		e->connect_timeout_ms,
		e->read_timeout_ms,
		e->keepalive_sock,
		&e->in_flight);
	if (st != OTLP_OK)
	{
		/* Build failed. The donated socket (if any) was closed by
		 * the build path. Drop our reference so we reconnect next
		 * time. */
		e->keepalive_sock = NULL;
		return st;
	}
	/* The request now owns the donated socket (if any). */
	e->keepalive_sock = NULL;
	e->in_flight_signal = p->signal_kind;
	e->in_flight_count = p->pending_count;
	/* IMPORTANT: do NOT free the pending batch here. It must stay
	 * alive until the in-flight request completes successfully or
	 * is permanently dropped, so retry can re-encode it. The batch
	 * is freed in record_outcome on success / permanent-failure
	 * paths. */
	*p->first_set = false;
	return OTLP_OK;
}

static otlp_status_t
try_start_post(struct otlp_exporter *e)
{
	struct signal_start_path p = {
		.pending = e->pending,
		.pending_count = e->pending_count,
		.first_set = &e->first_pending_set,
		.signal_kind = SIGNAL_SPAN,
		.build_request = build_span_request_void,
	};
	return try_start_post_common(e, &p);
}

static otlp_status_t
try_start_metric_post(struct otlp_exporter *e)
{
	struct signal_start_path p = {
		.pending = e->metric_pending,
		.pending_count = e->metric_pending_count,
		.first_set = &e->metric_first_set,
		.signal_kind = SIGNAL_METRIC,
		.build_request = build_metric_request_void,
	};
	return try_start_post_common(e, &p);
}

static otlp_status_t
try_start_log_post(struct otlp_exporter *e)
{
	struct signal_start_path p = {
		.pending = e->log_pending,
		.pending_count = e->log_pending_count,
		.first_set = &e->log_first_set,
		.signal_kind = SIGNAL_LOG,
		.build_request = build_log_request_void,
	};
	return try_start_post_common(e, &p);
}

/* What the in-flight HTTP exchange produced, handed to
 * record_outcome as one bundle. body/body_len are the response body
 * (NULL/0 when there is none — network failure, null transport). */
struct http_outcome
{
	int status; /* HTTP status; 0 = network-level failure */
	uint32_t retry_after_ms; /* Retry-After header value (v0.5.95) */
	const uint8_t *body;
	size_t body_len;
};

/* Surface a collector PartialSuccess report — the OTLP mechanism
 * for server-side data loss on an otherwise-successful export (the
 * collector accepted the request but rejected some items: queue
 * full, size limits, ...). Counts into the per-signal rejected_*
 * stat (when a counter is given — the sync one-shot path has none)
 * and logs WARN. The batch is NOT retried: a 200 is final, and the
 * rejected items are gone server-side. */
static void
report_partial_success(struct otlp_exporter *e,
	otlp_signal_id_t signal,
	otlp_atomic_u64 *rejected_counter,
	uint64_t count,
	const uint8_t *body,
	size_t body_len)
{
	otlp_event_t ev = {
		.code = OTLP_EVT_PARTIAL_SUCCESS,
		.level = OTLP_LOG_WARN,
		.signal = signal,
		.count = count,
	};
	int64_t rejected = 0;
	const char *msg = NULL;
	size_t msg_len = 0;

	if (!otlp_exporter_otel_decode_partial_success(
		    body, body_len, &rejected, &msg, &msg_len))
		return;
	if (rejected <= 0 && msg_len == 0)
		return;
	if (rejected > 0)
	{
		ev.rejected = (uint64_t) rejected;
		if (rejected_counter)
			otlp_atomic_fetch_add_u64(rejected_counter,
				(uint64_t) rejected,
				OTLP_MEMORY_ORDER_RELAXED);
	}
	if (msg_len > 0 && msg)
	{
		ev.detail = msg;
		ev.detail_len = msg_len;
	}
	event_log(e, &ev);
}

static void
record_outcome(struct otlp_exporter *e, const struct http_outcome *o)
{
	struct signal_record_path p = record_path_for(e);
	uint64_t count = e->in_flight_count;
	int http_status = o->status;

	if (http_status == 0)
	{
		/* Network-level failure (no HTTP response received).
		 * Treat as transient — same retry path as 5xx. (No
		 * Retry-After: there was no response to carry one.) */
		otlp_atomic_fetch_add_u64(
			&e->network_err, 1, OTLP_MEMORY_ORDER_RELAXED);
		e->attempt++;
		if (e->attempt > e->max_retries)
		{
			otlp_event_t ev = {
				.code = OTLP_EVT_ITEMS_DROPPED,
				.level = OTLP_LOG_ERROR,
				.signal = p.signal,
				.count = count,
				.max_retries = e->max_retries,
				.drop_reason = OTLP_DROP_MAX_RETRIES,
			};

			add_dropped_err_for_signal(&p, count);
			event_log(e, &ev);
			clear_in_flight_batch(e, &p);
			return;
		}
		{
			uint32_t delay = backoff_delay_ms(e, e->attempt);
			otlp_event_t ev = {
				.code = OTLP_EVT_RETRY_ARMED,
				.level = OTLP_LOG_WARN,
				.signal = p.signal,
				.attempt = e->attempt,
				.max_retries = e->max_retries,
				.delay_ms = delay,
			};

			e->backoff_deadline_mono = now_mono_ms() + delay;
			e->backoff_armed = true;
			event_log(e, &ev);
		}
		return;
	}
	if (http_status >= 200 && http_status < 300)
	{
		otlp_atomic_fetch_add_u64(
			&e->http_2xx, 1, OTLP_MEMORY_ORDER_RELAXED);
		add_sent_for_signal(&p, count);
		{
			otlp_event_t ev = {
				.code = OTLP_EVT_BATCH_SENT,
				.level = OTLP_LOG_DEBUG,
				.signal = p.signal,
				.count = count,
				.http_status = http_status,
			};

			event_log(e, &ev);
		}
		/* A 200 can still report server-side data loss via
		 * PartialSuccess in the response body (v0.5.96). */
		if (o->body && o->body_len > 0)
			report_partial_success(e,
				p.signal,
				p.rejected_counter,
				count,
				o->body,
				o->body_len);
		/* Success — free the pending batch (kept across retries). */
		clear_in_flight_batch(e, &p);
		return;
	}
	if (http_status == 429 || (http_status >= 500 && http_status < 600))
	{
		/* 429 is retryable but still a 4xx — count it in its own
		 * status-class bucket (http_4xx), not http_5xx. */
		otlp_atomic_fetch_add_u64(
			http_status == 429 ? &e->http_4xx : &e->http_5xx,
			1,
			OTLP_MEMORY_ORDER_RELAXED);
		e->attempt++;
		if (e->attempt > e->max_retries)
		{
			otlp_event_t ev = {
				.code = OTLP_EVT_ITEMS_DROPPED,
				.level = OTLP_LOG_ERROR,
				.signal = p.signal,
				.count = count,
				.http_status = http_status,
				.max_retries = e->max_retries,
				.drop_reason = OTLP_DROP_MAX_RETRIES,
			};

			add_dropped_err_for_signal(&p, count);
			event_log(e, &ev);
			/* Permanent failure — free the pending batch. */
			clear_in_flight_batch(e, &p);
		}
		else
		{
			/* Server-requested floor (RFC 7231 §7.1.3): never
			 * retry SOONER than Retry-After says, but never let
			 * a hostile/buggy server stall exports beyond our
			 * own cap — delay = max(jitter, Retry-After),
			 * clamped to backoff_max_ms. */
			uint32_t delay = backoff_delay_ms(e, e->attempt);
			bool server_driven = o->retry_after_ms > delay;

			if (server_driven)
				delay = o->retry_after_ms;
			if (delay > e->backoff_max_ms)
				delay = e->backoff_max_ms;
			{
				otlp_event_t ev = {
					.code = OTLP_EVT_RETRY_ARMED,
					.level = OTLP_LOG_WARN,
					.signal = p.signal,
					.http_status = http_status,
					.attempt = e->attempt,
					.max_retries = e->max_retries,
					.delay_ms = delay,
					.server_driven = server_driven,
				};

				e->backoff_deadline_mono =
					now_mono_ms() + delay;
				e->backoff_armed = true;
				event_log(e, &ev);
			}
		}
		return;
	}
	/* Permanent 4xx (non-429). */
	otlp_atomic_fetch_add_u64(&e->http_4xx, 1, OTLP_MEMORY_ORDER_RELAXED);
	add_dropped_err_for_signal(&p, count);
	{
		otlp_event_t ev = {
			.code = OTLP_EVT_ITEMS_DROPPED,
			.level = OTLP_LOG_ERROR,
			.signal = p.signal,
			.count = count,
			.http_status = http_status,
			.drop_reason = OTLP_DROP_HTTP_STATUS,
		};

		event_log(e, &ev);
	}
	/* Permanent failure — free the pending batch. */
	clear_in_flight_batch(e, &p);
}

otlp_status_t
otlp_exporter_tick(struct otlp_exporter *e, uint32_t max_wait_ms)
{
	uint64_t deadline;
	bool work_done;
	struct signal_path paths[3];
	int s;

	if (!e)
		return OTLP_ERR_NULL;

	/* Build the signal descriptor table once. All pointer fields
	 * point into the exporter struct; mutations through paths[]
	 * update e directly. */
	paths[SIGNAL_SPAN] = (struct signal_path){
		.queue = &e->queue,
		.pending = (void **) e->pending,
		.pending_cap = e->pending_cap,
		.pending_count = &e->pending_count,
		.first_set = &e->first_pending_set,
		.first_mono = &e->first_pending_mono,
		.signal_kind = SIGNAL_SPAN,
		.start_post = try_start_post,
	};
	paths[SIGNAL_METRIC] = (struct signal_path){
		.queue = &e->metric_queue,
		.pending = (void **) e->metric_pending,
		.pending_cap = e->metric_pending_cap,
		.pending_count = &e->metric_pending_count,
		.first_set = &e->metric_first_set,
		.first_mono = &e->metric_first_mono,
		.signal_kind = SIGNAL_METRIC,
		.start_post = try_start_metric_post,
	};
	paths[SIGNAL_LOG] = (struct signal_path){
		.queue = &e->log_queue,
		.pending = (void **) e->log_pending,
		.pending_cap = e->log_pending_cap,
		.pending_count = &e->log_pending_count,
		.first_set = &e->log_first_set,
		.first_mono = &e->log_first_mono,
		.signal_kind = SIGNAL_LOG,
		.start_post = try_start_log_post,
	};

	deadline = now_mono_ms() + max_wait_ms;

	do
	{
		work_done = false;

		/* 1. Drain all three queues into their pending arrays. */
		for (s = 0; s < 3; s++)
		{
			while (*paths[s].pending_count < paths[s].pending_cap)
			{
				void *item = mpsc_queue_pop(paths[s].queue);

				if (!item)
					break;
				paths[s].pending[(*paths[s].pending_count)++] =
					item;
				if (!*paths[s].first_set)
				{
					*paths[s].first_mono = now_mono_ms();
					*paths[s].first_set = true;
				}
				work_done = true;
			}
		}

		/* 2. Null-transport: try signals by priority. */
		if (e->null_transport && !e->backoff_armed)
		{
			for (s = 0; s < 3; s++)
			{
				if (*paths[s].pending_count > 0)
				{
					int http_status = 200;
					struct http_outcome o = { 0 };

					e->in_flight_signal =
						paths[s].signal_kind;
					e->in_flight_count =
						*paths[s].pending_count;
					if (e->null_transport_status_fn)
						http_status = e->null_transport_status_fn(
							e->null_transport_status_ctx);
					o.status = http_status;
					record_outcome(e, &o);
					work_done = true;
					goto tick_continue;
				}
			}
		}

		/* 3. Start POST if batch ready (by priority). */
		if (!e->in_flight && !e->backoff_armed)
		{
			bool shutdown =
				otlp_atomic_load_int(&e->shutdown_requested,
					OTLP_MEMORY_ORDER_RELAXED);
			uint64_t now_ms = now_mono_ms();

			for (s = 0; s < 3; s++)
			{
				if (e->in_flight || e->backoff_armed)
					break;
				if (*paths[s].pending_count >= e->batch_size ||
					(*paths[s].first_set &&
						now_ms - *paths[s].first_mono >=
							e->batch_ms) ||
					(shutdown &&
						*paths[s].pending_count > 0))
				{
					if (paths[s].start_post(e) == OTLP_OK)
						work_done = true;
				}
			}
		}

		/* 4. Step in-flight request. */
		if (e->in_flight)
		{
			otlp_status_t st = otlp_http_request_step(e->in_flight);
			otlp_http_req_state_t s2 =
				otlp_http_request_state(e->in_flight);

			if (s2 == OTLP_HTTP_REQ_DONE ||
				s2 == OTLP_HTTP_REQ_FAILED)
			{
				/* Read everything out BEFORE the free — the
				 * values live in the request's parsed state. */
				struct http_outcome o = { 0 };

				if (s2 == OTLP_HTTP_REQ_DONE)
				{
					o.status =
						otlp_http_request_http_status(
							e->in_flight);
					o.retry_after_ms =
						otlp_http_request_retry_after_ms(
							e->in_flight);
					o.body = otlp_http_request_body(
						e->in_flight, &o.body_len);
					e->keepalive_sock =
						otlp_http_request_detach_socket(
							e->in_flight);
				}
				record_outcome(e, &o);
				otlp_http_request_free(e->in_flight);
				e->in_flight = NULL;
				e->in_flight_count = 0;
				if (o.status != 0 &&
					(o.status < 200 || o.status >= 300) &&
					e->keepalive_sock)
				{
					otlp_socket_close(e->keepalive_sock);
					e->keepalive_sock = NULL;
				}
			}
			work_done = true;
			(void) st;
		}

		/* 5. Backoff retry (table-driven via paths[]). */
		if (e->backoff_armed && !e->in_flight &&
			now_mono_ms() >= e->backoff_deadline_mono)
		{
			e->backoff_armed = false;
			/* When null_transport is enabled, skip the HTTP
			 * retry — the next tick iteration's null-transport
			 * path (step 2) handles the retry cleanly. Without
			 * this check, the HTTP retry + null-transport
			 * double-processes the batch (double-counting in
			 * stats). */
			if (!e->null_transport)
				paths[e->in_flight_signal].start_post(e);
			if (e->in_flight)
				work_done = true;
		}

		/* 6. Sleep if waiting on backoff. */
		if (!work_done && e->backoff_armed && !e->in_flight)
		{
#if defined(_WIN32)
			Sleep(1);
#else
			struct timespec ts = { 0, 1 * 1000 * 1000 };
			nanosleep(&ts, NULL);
#endif
		}

	tick_continue:;
	} while (work_done && now_mono_ms() < deadline);

	return OTLP_OK;
}

otlp_status_t
otlp_exporter_flush(otlp_exporter_t *e)
{
	uint64_t deadline;
	uint64_t now;

	if (!e)
		return OTLP_ERR_NULL;
	deadline = now_mono_ms() + e->flush_timeout_ms;

	do
	{
		otlp_exporter_tick(e, 100);
		now = now_mono_ms();
	} while ((e->pending_count > 0 || e->in_flight ||
			 mpsc_queue_size(&e->queue) > 0 ||
			 e->metric_pending_count > 0 ||
			 mpsc_queue_size(&e->metric_queue) > 0 ||
			 e->log_pending_count > 0 ||
			 mpsc_queue_size(&e->log_queue) > 0) &&
		now < deadline);

	/* The return-status check must match the loop condition. The
	 * loop exits on "no work" OR "deadline reached"; if the
	 * deadline was reached with items still queued (drain cap hit,
	 * or a tight race after a POST completion cleared pending but
	 * before the next drain), the user needs to know items remain.
	 * Pre-v0.5.58 this check omitted the queue sizes, so flush
	 * could silently return OK with unsent items. */
	if (e->pending_count > 0 || e->in_flight ||
		e->metric_pending_count > 0 || e->log_pending_count > 0 ||
		mpsc_queue_size(&e->queue) > 0 ||
		mpsc_queue_size(&e->metric_queue) > 0 ||
		mpsc_queue_size(&e->log_queue) > 0)
		return OTLP_ERR_NETWORK;
	return OTLP_OK;
}

void
otlp_exporter_set_null_transport(otlp_exporter_t *e, bool enabled)
{
	if (e)
		e->null_transport = enabled;
}

void
otlp_exporter_set_null_transport_status_fn(otlp_exporter_t *e,
	otlp_null_transport_status_fn fn,
	void *ctx)
{
	if (e)
	{
		e->null_transport_status_fn = fn;
		e->null_transport_status_ctx = ctx;
	}
}

void
otlp_exporter_set_logger(otlp_exporter_t *e, otlp_log_fn fn, void *ctx)
{
	if (e)
	{
		e->log_fn = fn;
		e->log_ctx = ctx;
	}
}

void
otlp_exporter_set_event_logger(otlp_exporter_t *e, otlp_event_fn fn, void *ctx)
{
	if (e)
	{
		e->event_fn = fn;
		e->event_ctx = ctx;
	}
}

/* ── Synchronous metric / log flush ───────────────────────────── */

/* One POST attempt. Returns OTLP_OK on 2xx; sets *got_response
 * when the server answered at all (any HTTP status). A network-
 * level failure before a response (*got_response == false) is
 * transient — the caller retries with the async path's backoff
 * budget. A non-2xx response is permanent for the sync path. */
static otlp_status_t
flush_post_once(struct otlp_exporter *e,
	otlp_signal_id_t signal,
	const struct otlp_http_url *url,
	const char *path,
	const uint8_t *body,
	size_t body_len,
	bool *got_response)
{
	otlp_http_request_t *req = NULL;
	otlp_status_t st;
	uint64_t deadline;
	uint64_t now;
	struct otlp_http_url u;

	u = *url;
	snprintf(u.path, sizeof(u.path), "%s", path);
	st = otlp_http_request_start(&req,
		&u,
		e->user_agent,
		body,
		body_len,
		e->connect_timeout_ms,
		e->read_timeout_ms);
	if (st != OTLP_OK)
	{
		otlp_event_t ev = {
			.code = OTLP_EVT_SYNC_FLUSH_FAILED,
			.level = OTLP_LOG_ERROR,
			.signal = signal,
			.status = st,
		};

		event_log(e, &ev);
		return st;
	}
	deadline = now_mono_ms() + e->flush_timeout_ms;
	for (;;)
	{
		st = otlp_http_request_step(req);
		otlp_http_req_state_t s = otlp_http_request_state(req);

		if (s == OTLP_HTTP_REQ_DONE)
		{
			int http = otlp_http_request_http_status(req);

			if (http >= 200 && http < 300)
			{
				/* Surface server-reported data loss, if any
				 * (no stats counter on the one-shot path —
				 * the message is the observability surface).
				 * path + 5 skips "/v1/" to the signal name. */
				size_t blen = 0;
				const uint8_t *b =
					otlp_http_request_body(req, &blen);

				if (b && blen > 0)
					report_partial_success(
						e, signal, NULL, 0, b, blen);
			}
			otlp_http_request_free(req);
			*got_response = true;
			if (http >= 200 && http < 300)
				return OTLP_OK;
			otlp_event_t ev = {
				.code = OTLP_EVT_SYNC_FLUSH_FAILED,
				.level = OTLP_LOG_ERROR,
				.signal = signal,
				.http_status = http,
			};

			event_log(e, &ev);
			return OTLP_ERR_NETWORK;
		}
		if (s == OTLP_HTTP_REQ_FAILED)
		{
			otlp_event_t ev = {
				.code = OTLP_EVT_SYNC_FLUSH_FAILED,
				.level = OTLP_LOG_ERROR,
				.signal = signal,
				.status = OTLP_ERR_NETWORK,
			};

			otlp_http_request_free(req);
			event_log(e, &ev);
			return OTLP_ERR_NETWORK;
		}
		if (st != OTLP_OK && st != OTLP_ERR_WOULDBLOCK)
		{
			otlp_event_t ev = {
				.code = OTLP_EVT_SYNC_FLUSH_FAILED,
				.level = OTLP_LOG_ERROR,
				.signal = signal,
				.status = st,
			};

			otlp_http_request_free(req);
			event_log(e, &ev);
			return st;
		}
		now = now_mono_ms();
		if (now >= deadline)
		{
			otlp_event_t ev = {
				.code = OTLP_EVT_SYNC_FLUSH_FAILED,
				.level = OTLP_LOG_ERROR,
				.signal = signal,
				.status = OTLP_ERR_TIMEOUT,
				.timeout_ms = e->flush_timeout_ms,
			};

			otlp_http_request_free(req);
			event_log(e, &ev);
			return OTLP_ERR_TIMEOUT;
		}
#if defined(_WIN32)
		Sleep(1);
#else
		{
			struct timespec ts = { 0, 1000 * 1000 };
			nanosleep(&ts, NULL);
		}
#endif
	}
	otlp_http_request_free(req);
	return OTLP_ERR_TIMEOUT;
}

static otlp_status_t
flush_sync(struct otlp_exporter *e,
	otlp_signal_id_t signal,
	const char *path,
	const uint8_t *body,
	size_t body_len)
{
	otlp_status_t st = OTLP_ERR_NETWORK;
	bool got_response = false;
	uint32_t attempt;

	if (!e || !path || (!body && body_len > 0))
		return OTLP_ERR_NULL;
	if (e->null_transport)
		return OTLP_OK;
	/* Retry transient (pre-response) network failures with the
	 * same budget the async pipeline uses. The first connect in
	 * a fresh process occasionally fails transiently (DNS/order-
	 * of-addresses, collector still warming its accept queue);
	 * the async path recovers via backoff — the sync path
	 * deserves the same resilience. Non-2xx responses and
	 * timeouts are permanent (no retry). */
	for (attempt = 0; attempt <= e->max_retries; attempt++)
	{
		st = flush_post_once(e,
			signal,
			&e->url,
			path,
			body,
			body_len,
			&got_response);
		if (st == OTLP_OK || got_response)
			return st;
		if (attempt < e->max_retries)
		{
			uint32_t delay = e->backoff_initial_ms;
			otlp_event_t ev = {
				.code = OTLP_EVT_RETRY_ARMED,
				.level = OTLP_LOG_WARN,
				.signal = signal,
				.attempt = attempt + 1,
				.max_retries = e->max_retries,
				.delay_ms = delay > 100 ? 100 : delay,
			};

			if (delay > 100)
				delay = 100; /* sync path: short backoff */
			ev.delay_ms = delay;
			event_log(e, &ev);
#if defined(_WIN32)
			Sleep(delay);
#else
			{
				struct timespec ts = { 0,
					(long) delay * 1000 * 1000 };
				nanosleep(&ts, NULL);
			}
#endif
		}
	}
	return st;
}

otlp_status_t
otlp_exporter_flush_metric(otlp_exporter_t *e, const otlp_metric_t *m)
{
	struct otlp_pb_buf body = { 0 };
	otlp_status_t st;
	const otlp_metric_t *arr[1];

	if (!e || !m)
		return OTLP_ERR_NULL;
	otlp_atomic_fetch_add_u64(
		&e->emitted_metrics, 1, OTLP_MEMORY_ORDER_RELAXED);
	st = otlp_pb_buf_init(&body, 0);
	if (st != OTLP_OK)
	{
		/* Accounting invariant: emitted == sent + dropped_err.
		 * Pre-v0.5.59 this path returned without updating
		 * dropped_err, breaking the invariant under OOM. */
		otlp_atomic_fetch_add_u64(
			&e->dropped_metrics_err, 1, OTLP_MEMORY_ORDER_RELAXED);
		return st;
	}
	arr[0] = m;
	st = otlp_encode_export_metrics_service_request(&body,
		e->service_name,
		e->resource_attrs.items,
		e->resource_attrs.n,
		NULL,
		NULL,
		arr,
		1);
	if (st == OTLP_OK)
		st = flush_sync(e,
			OTLP_SIGNAL_METRICS,
			"/v1/metrics",
			body.data,
			body.len);
	otlp_pb_buf_free(&body);
	if (st == OTLP_OK)
		otlp_atomic_fetch_add_u64(
			&e->sent_metrics, 1, OTLP_MEMORY_ORDER_RELAXED);
	else
		otlp_atomic_fetch_add_u64(
			&e->dropped_metrics_err, 1, OTLP_MEMORY_ORDER_RELAXED);
	return st;
}

otlp_status_t
otlp_exporter_flush_log(otlp_exporter_t *e, const otlp_log_record_t *lr)
{
	struct otlp_pb_buf body = { 0 };
	otlp_status_t st;
	const otlp_log_record_t *arr[1];

	if (!e || !lr)
		return OTLP_ERR_NULL;
	otlp_atomic_fetch_add_u64(
		&e->emitted_logs, 1, OTLP_MEMORY_ORDER_RELAXED);
	st = otlp_pb_buf_init(&body, 0);
	if (st != OTLP_OK)
	{
		/* Accounting invariant: emitted == sent + dropped_err.
		 * Pre-v0.5.59 this path returned without updating
		 * dropped_err, breaking the invariant under OOM. */
		otlp_atomic_fetch_add_u64(
			&e->dropped_logs_err, 1, OTLP_MEMORY_ORDER_RELAXED);
		return st;
	}
	arr[0] = lr;
	st = otlp_encode_export_logs_service_request(&body,
		e->service_name,
		e->resource_attrs.items,
		e->resource_attrs.n,
		NULL,
		NULL,
		arr,
		1);
	if (st == OTLP_OK)
		st = flush_sync(
			e, OTLP_SIGNAL_LOGS, "/v1/logs", body.data, body.len);
	otlp_pb_buf_free(&body);
	if (st == OTLP_OK)
		otlp_atomic_fetch_add_u64(
			&e->sent_logs, 1, OTLP_MEMORY_ORDER_RELAXED);
	else
		otlp_atomic_fetch_add_u64(
			&e->dropped_logs_err, 1, OTLP_MEMORY_ORDER_RELAXED);
	return st;
}

otlp_status_t
otlp_exporter_shutdown(otlp_exporter_t *e)
{
	if (!e)
		return OTLP_ERR_NULL;
	otlp_atomic_store_int(
		&e->shutdown_requested, 1, OTLP_MEMORY_ORDER_RELEASE);
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
	/* Argument validation BEFORE the state check: out=NULL with
	 * cap>0 is a caller bug regardless of whether a request is
	 * in flight (previously returned OK+n=0 in that case — a
	 * contract wart the first poll_fds test caught). */
	if (!out && cap > 0)
	{
		*n_out = 0;
		return OTLP_ERR_NULL;
	}
	if (!e->in_flight || cap == 0)
	{
		*n_out = 0;
		return OTLP_OK;
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
	out->emitted =
		otlp_atomic_load_u64(&e->emitted, OTLP_MEMORY_ORDER_RELAXED);
	out->dropped_full = otlp_atomic_load_u64(
		&e->dropped_full, OTLP_MEMORY_ORDER_RELAXED);
	out->dropped_err = otlp_atomic_load_u64(
		&e->dropped_err, OTLP_MEMORY_ORDER_RELAXED);
	out->sent = otlp_atomic_load_u64(&e->sent, OTLP_MEMORY_ORDER_RELAXED);
	out->http_2xx =
		otlp_atomic_load_u64(&e->http_2xx, OTLP_MEMORY_ORDER_RELAXED);
	out->http_4xx =
		otlp_atomic_load_u64(&e->http_4xx, OTLP_MEMORY_ORDER_RELAXED);
	out->http_5xx =
		otlp_atomic_load_u64(&e->http_5xx, OTLP_MEMORY_ORDER_RELAXED);
	out->network_err = otlp_atomic_load_u64(
		&e->network_err, OTLP_MEMORY_ORDER_RELAXED);
	out->emitted_metrics = otlp_atomic_load_u64(
		&e->emitted_metrics, OTLP_MEMORY_ORDER_RELAXED);
	out->sent_metrics = otlp_atomic_load_u64(
		&e->sent_metrics, OTLP_MEMORY_ORDER_RELAXED);
	out->dropped_metrics_full = otlp_atomic_load_u64(
		&e->dropped_metrics_full, OTLP_MEMORY_ORDER_RELAXED);
	out->dropped_metrics_err = otlp_atomic_load_u64(
		&e->dropped_metrics_err, OTLP_MEMORY_ORDER_RELAXED);
	out->emitted_logs = otlp_atomic_load_u64(
		&e->emitted_logs, OTLP_MEMORY_ORDER_RELAXED);
	out->sent_logs =
		otlp_atomic_load_u64(&e->sent_logs, OTLP_MEMORY_ORDER_RELAXED);
	out->dropped_logs_full = otlp_atomic_load_u64(
		&e->dropped_logs_full, OTLP_MEMORY_ORDER_RELAXED);
	out->dropped_logs_err = otlp_atomic_load_u64(
		&e->dropped_logs_err, OTLP_MEMORY_ORDER_RELAXED);
	out->rejected_spans = otlp_atomic_load_u64(
		&e->rejected_spans, OTLP_MEMORY_ORDER_RELAXED);
	out->rejected_metrics = otlp_atomic_load_u64(
		&e->rejected_metrics, OTLP_MEMORY_ORDER_RELAXED);
	out->rejected_logs = otlp_atomic_load_u64(
		&e->rejected_logs, OTLP_MEMORY_ORDER_RELAXED);
	return OTLP_OK;
}
