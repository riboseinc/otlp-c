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
#define OTLP_DEFAULT_BATCH_MS 100
#define OTLP_DEFAULT_MAX_RETRIES 5
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
	otlp_resource_attr_t *resource_attributes;
	size_t n_resource_attributes;
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
	otlp_atomic_int shutdown_requested;

	/* Optional diagnostic callback (NULL = no-op). */
	otlp_log_fn log_fn;
	void       *log_ctx;

	/* Tick-private state (no synchronisation needed). */
	otlp_span_t **pending;
	size_t pending_cap;
	size_t pending_count;
	bool first_pending_set;
	uint64_t first_pending_mono;

	/* Metric/log pending batches (tick-private). */
	otlp_metric_t     **metric_pending;
	size_t		metric_pending_cap;
	size_t		metric_pending_count;
	bool		metric_first_set;
	uint64_t		metric_first_mono;
	otlp_log_record_t **log_pending;
	size_t		log_pending_cap;
	size_t		log_pending_count;
	bool		log_first_set;
	uint64_t		log_first_mono;

	/* In-flight request state. in_flight_signal identifies which
	 * signal's batch is being POSTed (0=span, 1=metric, 2=log). */
	otlp_http_request_t *in_flight;
	int			 in_flight_signal;
	size_t			 in_flight_count;
	uint32_t		 attempt;
	uint64_t		 backoff_deadline_mono;
	bool			 backoff_armed;
	/* Cached TCP connection for HTTP keep-alive. Owned by the exporter,
	 * donated to the next in_flight request, re-acquired on success. */
	otlp_socket_t *keepalive_sock;
	bool null_transport;
	otlp_null_transport_status_fn null_transport_status_fn;
	void *null_transport_status_ctx;
};

/* ── Helpers ──────────────────────────────────────────────────── */

static uint64_t
now_mono_ms(void)
{
	uint64_t n;

	return (otlp_platform_now_mono_nano(&n) == OTLP_OK) ? n / 1000000ULL
							    : 0;
}

/* Diagnostic logger. No-op when exp->log_fn is NULL (zero overhead:
 * the check happens before any formatting work). */
static void
otlp_log(const struct otlp_exporter *e,
	 otlp_log_level_t		 level,
	 const char			*fmt,
	 ...)
{
	char	 buf[256];
	va_list ap;

	if (!e || !e->log_fn)
		return;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	e->log_fn(e->log_ctx, level, buf);
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
	if (o->flush_timeout_ms == 0)
		o->flush_timeout_ms = OTLP_DEFAULT_FLUSH_TIMEOUT_MS;
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
	if (o.n_resource_attributes > 0 && o.resource_attributes)
	{
		size_t i;
		e->resource_attributes =
			otlp_malloc(o.n_resource_attributes *
				    sizeof(*e->resource_attributes));
		if (!e->resource_attributes)
			goto fail;
		e->n_resource_attributes = o.n_resource_attributes;
		for (i = 0; i < o.n_resource_attributes; i++)
		{
			const otlp_resource_attr_t *src = &o.resource_attributes[i];
			otlp_resource_attr_t       *dst = &e->resource_attributes[i];

			dst->key = otlp_dup_str(src->key);
			if (!dst->key)
				goto fail;
			dst->type       = src->type;
			dst->int64_val  = src->int64_val;
			dst->double_val = src->double_val;
			dst->bool_val   = src->bool_val;
			/* Always copy value — for STRING it's the string; for
			 * other types it's NULL/unused but copying keeps the
			 * free path uniform (always free key + value). */
			dst->value = src->value ? otlp_dup_str(src->value) : NULL;
			if (src->value && !dst->value)
				goto fail;
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
	e->metric_pending = otlp_malloc(
		e->metric_pending_cap * sizeof(*e->metric_pending));
	if (!e->metric_pending)
		goto fail;

	e->log_pending_cap = e->batch_size * 2;
	e->log_pending = otlp_malloc(
		e->log_pending_cap * sizeof(*e->log_pending));
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
	otlp_atomic_store_int(&e->shutdown_requested, 0, OTLP_MEMORY_ORDER_RELEASE);
	return e;

fail:
	otlp_free(e->user_agent);
	otlp_free(e->service_name);
	if (e->resource_attributes)
	{
		size_t i;
		for (i = 0; i < e->n_resource_attributes; i++)
		{
			otlp_free((char *) e->resource_attributes[i].key);
			otlp_free((char *) e->resource_attributes[i].value);
		}
		otlp_free(e->resource_attributes);
	}
	otlp_free(e->pending);
	otlp_free(e->metric_pending);
	otlp_free(e->log_pending);
	otlp_free(e);
	return NULL;
}

void
otlp_exporter_free(otlp_exporter_t *e)
{
	otlp_span_t *span;
	otlp_metric_t *metric;
	otlp_log_record_t *log;
	size_t i;

	if (!e)
		return;
	/* Drain the queues, freeing any un-sent items. */
	while ((span = mpsc_queue_pop(&e->queue)) != NULL)
		otlp_span_free(span);
	while ((metric = mpsc_queue_pop(&e->metric_queue)) != NULL)
		otlp_metric_free(metric);
	while ((log = mpsc_queue_pop(&e->log_queue)) != NULL)
		otlp_log_record_free(log);
	/* Free pending batches. */
	free_pending_batch(e->pending, e->pending_count);
	for (i = 0; i < e->metric_pending_count; i++)
		otlp_metric_free(e->metric_pending[i]);
	for (i = 0; i < e->log_pending_count; i++)
		otlp_log_record_free(e->log_pending[i]);
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
	if (e->resource_attributes)
	{
		for (i = 0; i < e->n_resource_attributes; i++)
		{
			otlp_free((char *) e->resource_attributes[i].key);
			otlp_free((char *) e->resource_attributes[i].value);
		}
		otlp_free(e->resource_attributes);
	}
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
	if (otlp_atomic_load_int(&e->shutdown_requested, OTLP_MEMORY_ORDER_ACQUIRE))
		return OTLP_ERR_SHUTDOWN;

	clone = otlp_span_clone(span);
	if (!clone)
		return OTLP_ERR_NOMEM;
	st = mpsc_queue_push(&e->queue, clone);
	if (st != OTLP_OK)
	{
		otlp_span_free(clone);
		otlp_atomic_fetch_add_u64(
			&e->dropped_full, 1, OTLP_MEMORY_ORDER_RELAXED);
		otlp_log(e, OTLP_LOG_WARN,
			 "span dropped: queue full (size=%zu)",
			 mpsc_queue_size(&e->queue));
		return st;
	}
	otlp_atomic_fetch_add_u64(&e->emitted, 1, OTLP_MEMORY_ORDER_RELAXED);
	return OTLP_OK;
}

otlp_status_t
otlp_exporter_emit_move(otlp_exporter_t *e, otlp_span_t *span)
{
	otlp_status_t st;

	if (!e || !span)
		return OTLP_ERR_NULL;
	if (otlp_atomic_load_int(&e->shutdown_requested, OTLP_MEMORY_ORDER_ACQUIRE))
		return OTLP_ERR_SHUTDOWN;

	st = mpsc_queue_push(&e->queue, span);
	if (st != OTLP_OK)
	{
		otlp_span_free(span);
		otlp_atomic_fetch_add_u64(
			&e->dropped_full, 1, OTLP_MEMORY_ORDER_RELAXED);
		otlp_log(e, OTLP_LOG_WARN,
			 "span dropped: queue full (size=%zu)",
			 mpsc_queue_size(&e->queue));
		return st;
	}
	otlp_atomic_fetch_add_u64(&e->emitted, 1, OTLP_MEMORY_ORDER_RELAXED);
	return OTLP_OK;
}

otlp_status_t
otlp_exporter_emit_metric_move(otlp_exporter_t *e, otlp_metric_t *metric)
{
	otlp_status_t st;

	if (!e || !metric)
		return OTLP_ERR_NULL;
	if (otlp_atomic_load_int(&e->shutdown_requested, OTLP_MEMORY_ORDER_ACQUIRE))
		return OTLP_ERR_SHUTDOWN;

	st = mpsc_queue_push(&e->metric_queue, metric);
	if (st != OTLP_OK)
	{
		otlp_metric_free(metric);
		otlp_atomic_fetch_add_u64(
			&e->dropped_metrics_full, 1, OTLP_MEMORY_ORDER_RELAXED);
		otlp_log(e, OTLP_LOG_WARN,
			 "metric dropped: queue full (size=%zu)",
			 mpsc_queue_size(&e->metric_queue));
		return st;
	}
	otlp_atomic_fetch_add_u64(
		&e->emitted_metrics, 1, OTLP_MEMORY_ORDER_RELAXED);
	return OTLP_OK;
}

otlp_status_t
otlp_exporter_emit_log_move(otlp_exporter_t *e, otlp_log_record_t *log)
{
	otlp_status_t st;

	if (!e || !log)
		return OTLP_ERR_NULL;
	if (otlp_atomic_load_int(&e->shutdown_requested, OTLP_MEMORY_ORDER_ACQUIRE))
		return OTLP_ERR_SHUTDOWN;

	st = mpsc_queue_push(&e->log_queue, log);
	if (st != OTLP_OK)
	{
		otlp_log_record_free(log);
		otlp_atomic_fetch_add_u64(
			&e->dropped_logs_full, 1, OTLP_MEMORY_ORDER_RELAXED);
		otlp_log(e, OTLP_LOG_WARN,
			 "log dropped: queue full (size=%zu)",
			 mpsc_queue_size(&e->log_queue));
		return st;
	}
	otlp_atomic_fetch_add_u64(
		&e->emitted_logs, 1, OTLP_MEMORY_ORDER_RELAXED);
	return OTLP_OK;
}

/* ── tick (single thread) ─────────────────────────────────────── */

/* Signal kind constants (0=span, 1=metric, 2=log). */
enum { SIGNAL_SPAN = 0, SIGNAL_METRIC = 1, SIGNAL_LOG = 2 };

/* Clear the pending batch for whichever signal is in-flight. */
static void
clear_in_flight_batch(struct otlp_exporter *e)
{
	size_t i;
	switch (e->in_flight_signal)
	{
		case SIGNAL_SPAN:
			free_pending_batch(e->pending, e->pending_count);
			e->pending_count = 0;
			e->first_pending_set = false;
			break;
		case SIGNAL_METRIC:
			for (i = 0; i < e->metric_pending_count; i++)
				otlp_metric_free(e->metric_pending[i]);
			e->metric_pending_count = 0;
			e->metric_first_set = false;
			break;
		case SIGNAL_LOG:
			for (i = 0; i < e->log_pending_count; i++)
				otlp_log_record_free(e->log_pending[i]);
			e->log_pending_count = 0;
			e->log_first_set = false;
			break;
	}
	/* Reset retry state for the next batch. */
	e->attempt = 0;
	e->backoff_armed = false;
}

static void
add_sent_for_signal(struct otlp_exporter *e)
{
	switch (e->in_flight_signal)
	{
		case SIGNAL_SPAN:
			otlp_atomic_fetch_add_u64(
				&e->sent, e->in_flight_count, OTLP_MEMORY_ORDER_RELAXED);
			break;
		case SIGNAL_METRIC:
			otlp_atomic_fetch_add_u64(&e->sent_metrics,
				e->in_flight_count, OTLP_MEMORY_ORDER_RELAXED);
			break;
		case SIGNAL_LOG:
			otlp_atomic_fetch_add_u64(&e->sent_logs,
				e->in_flight_count, OTLP_MEMORY_ORDER_RELAXED);
			break;
	}
}

static void
add_dropped_err_for_signal(struct otlp_exporter *e)
{
	switch (e->in_flight_signal)
	{
		case SIGNAL_SPAN:
			otlp_atomic_fetch_add_u64(&e->dropped_err,
				e->in_flight_count, OTLP_MEMORY_ORDER_RELAXED);
			break;
		case SIGNAL_METRIC:
			otlp_atomic_fetch_add_u64(&e->dropped_metrics_err,
				e->in_flight_count, OTLP_MEMORY_ORDER_RELAXED);
			break;
		case SIGNAL_LOG:
			otlp_atomic_fetch_add_u64(&e->dropped_logs_err,
				e->in_flight_count, OTLP_MEMORY_ORDER_RELAXED);
			break;
	}
}

static otlp_status_t
try_start_post(struct otlp_exporter *e)
{
	otlp_status_t st;

	if (e->in_flight || e->pending_count == 0)
		return OTLP_OK;
	st = otlp_exporter_otel_build_request(&e->url,
		e->user_agent,
		e->service_name,
		e->resource_attributes,
		e->n_resource_attributes,
		(const otlp_span_t *const *) e->pending,
		e->pending_count,
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
	e->in_flight_signal = SIGNAL_SPAN;
	e->in_flight_count = e->pending_count;
	/* IMPORTANT: do NOT free the pending batch here. It must stay
	 * alive until the in-flight request completes successfully or
	 * is permanently dropped, so retry can re-encode it. The batch
	 * is freed in record_outcome on success / permanent-failure
	 * paths. */
	e->first_pending_set = false;
	return OTLP_OK;
}

/* Start a POST for the metric batch. Encodes ExportMetricsServiceRequest
 * and opens an HTTP request to /v1/metrics. The metric_pending array
 * stays alive until record_outcome clears it. */
static otlp_status_t
try_start_metric_post(struct otlp_exporter *e)
{
	struct otlp_pb_buf    body = { 0 };
	struct otlp_http_url  url;
	otlp_status_t	     st;

	if (e->in_flight || e->metric_pending_count == 0)
		return OTLP_OK;
	st = otlp_encode_export_metrics_service_request(
		&body, e->service_name,
		e->resource_attributes, e->n_resource_attributes,
		NULL, NULL,
		(const otlp_metric_t *const *) e->metric_pending,
		e->metric_pending_count);
	if (st != OTLP_OK)
		return st;
	url = e->url;
	snprintf(url.path, sizeof(url.path), "/v1/metrics");
	st = otlp_http_request_start(&e->in_flight, &url,
		e->user_agent, body.data, body.len,
		e->connect_timeout_ms, e->read_timeout_ms);
	otlp_pb_buf_free(&body);
	if (st != OTLP_OK)
		return st;
	e->in_flight_signal = SIGNAL_METRIC;
	e->in_flight_count  = e->metric_pending_count;
	e->metric_first_set = false;
	return OTLP_OK;
}

/* Start a POST for the log batch. Same pattern as metrics, to /v1/logs. */
static otlp_status_t
try_start_log_post(struct otlp_exporter *e)
{
	struct otlp_pb_buf    body = { 0 };
	struct otlp_http_url  url;
	otlp_status_t	     st;

	if (e->in_flight || e->log_pending_count == 0)
		return OTLP_OK;
	st = otlp_encode_export_logs_service_request(
		&body, e->service_name,
		e->resource_attributes, e->n_resource_attributes,
		NULL, NULL,
		(const otlp_log_record_t *const *) e->log_pending,
		e->log_pending_count);
	if (st != OTLP_OK)
		return st;
	url = e->url;
	snprintf(url.path, sizeof(url.path), "/v1/logs");
	st = otlp_http_request_start(&e->in_flight, &url,
		e->user_agent, body.data, body.len,
		e->connect_timeout_ms, e->read_timeout_ms);
	otlp_pb_buf_free(&body);
	if (st != OTLP_OK)
		return st;
	e->in_flight_signal = SIGNAL_LOG;
	e->in_flight_count  = e->log_pending_count;
	e->log_first_set    = false;
	return OTLP_OK;
}

static void
record_outcome(struct otlp_exporter *e, int http_status)
{
	const char *signal_name =
		e->in_flight_signal == SIGNAL_METRIC ? "metrics" :
		e->in_flight_signal == SIGNAL_LOG    ? "logs"    : "spans";

	if (http_status == 0)
	{
		/* Network-level failure (no HTTP response received).
		 * Treat as transient — same retry path as 5xx. */
		otlp_atomic_fetch_add_u64(
			&e->network_err, 1, OTLP_MEMORY_ORDER_RELAXED);
		e->attempt++;
		if (e->attempt > e->max_retries)
		{
			add_dropped_err_for_signal(e);
			otlp_log(e, OTLP_LOG_ERROR,
				 "network error: %llu %s dropped (max retries %u)",
				 (unsigned long long) e->in_flight_count,
				 signal_name, e->max_retries);
			clear_in_flight_batch(e);
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
			otlp_log(e, OTLP_LOG_WARN,
				 "network error; retry %u/%u in %ums",
				 e->attempt, e->max_retries, delay);
		}
		return;
	}
	if (http_status >= 200 && http_status < 300)
	{
		otlp_atomic_fetch_add_u64(
			&e->http_2xx, 1, OTLP_MEMORY_ORDER_RELAXED);
		add_sent_for_signal(e);
		otlp_log(e, OTLP_LOG_DEBUG,
			 "batch sent: %llu %s",
			 (unsigned long long) e->in_flight_count, signal_name);
		/* Success — free the pending batch (kept across retries). */
		clear_in_flight_batch(e);
		return;
	}
	if (http_status == 429 || (http_status >= 500 && http_status < 600))
	{
		otlp_atomic_fetch_add_u64(
			&e->http_5xx, 1, OTLP_MEMORY_ORDER_RELAXED);
		e->attempt++;
		if (e->attempt > e->max_retries)
		{
			add_dropped_err_for_signal(e);
			otlp_log(e, OTLP_LOG_ERROR,
				 "HTTP %d: %llu %s dropped (max retries %u)",
				 http_status,
				 (unsigned long long) e->in_flight_count,
				 signal_name, e->max_retries);
			/* Permanent failure — free the pending batch. */
			clear_in_flight_batch(e);
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
			otlp_log(e, OTLP_LOG_WARN,
				 "HTTP %d; retry %u/%u in %ums",
				 http_status, e->attempt, e->max_retries,
				 delay);
		}
		return;
	}
	/* Permanent 4xx (non-429). */
	otlp_atomic_fetch_add_u64(&e->http_4xx, 1, OTLP_MEMORY_ORDER_RELAXED);
	add_dropped_err_for_signal(e);
	otlp_log(e, OTLP_LOG_ERROR,
		 "HTTP %d: %llu %s dropped (permanent)",
		 http_status, (unsigned long long) e->in_flight_count,
		 signal_name);
	/* Permanent failure — free the pending batch. */
	clear_in_flight_batch(e);
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

		/* 1. Drain span queue into pending. */
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

		/* 1a. Drain metric queue. */
		while (e->metric_pending_count < e->metric_pending_cap)
		{
			otlp_metric_t *m = mpsc_queue_pop(&e->metric_queue);

			if (!m)
				break;
			e->metric_pending[e->metric_pending_count++] = m;
			if (!e->metric_first_set)
			{
				e->metric_first_mono = now_mono_ms();
				e->metric_first_set = true;
			}
			work_done = true;
		}

		/* 1b. Drain log queue. */
		while (e->log_pending_count < e->log_pending_cap)
		{
			otlp_log_record_t *lr = mpsc_queue_pop(&e->log_queue);

			if (!lr)
				break;
			e->log_pending[e->log_pending_count++] = lr;
			if (!e->log_first_set)
			{
				e->log_first_mono = now_mono_ms();
				e->log_first_set = true;
			}
			work_done = true;
		}

		/* 1c. Null-transport fast path: try span, then metric, then
		 * log. Respects backoff_armed so retry/backoff behavior is
		 * testable via the null_transport status callback. */
		if (e->null_transport && !e->backoff_armed) {
			int http_status = 200;
			bool have_work = false;

			if (e->pending_count > 0) {
				e->in_flight_signal = SIGNAL_SPAN;
				e->in_flight_count = e->pending_count;
				have_work = true;
			} else if (e->metric_pending_count > 0) {
				e->in_flight_signal = SIGNAL_METRIC;
				e->in_flight_count = e->metric_pending_count;
				have_work = true;
			} else if (e->log_pending_count > 0) {
				e->in_flight_signal = SIGNAL_LOG;
				e->in_flight_count = e->log_pending_count;
				have_work = true;
			}
			if (have_work) {
				if (e->null_transport_status_fn)
					http_status = e->null_transport_status_fn(
					    e->null_transport_status_ctx);
				record_outcome(e, http_status);
				work_done = true;
				continue;
			}
		}

		/* 2. Start POST if batch ready — try span, then metric, then
		 * log. Only one in-flight request at a time (shared across
		 * all signals). */
		if (!e->in_flight && !e->backoff_armed)
		{
			bool shutdown = otlp_atomic_load_int(
				&e->shutdown_requested,
				OTLP_MEMORY_ORDER_RELAXED);
			uint64_t now_ms = now_mono_ms();

			/* Span batch ready? */
			if (e->pending_count >= e->batch_size ||
			    (e->first_pending_set &&
			     now_ms - e->first_pending_mono >= e->batch_ms) ||
			    (shutdown && e->pending_count > 0))
			{
				if (try_start_post(e) == OTLP_OK)
					work_done = true;
			}
			/* If span didn't start a POST, try metric. */
			if (!e->in_flight && !e->backoff_armed &&
			    (e->metric_pending_count >= e->batch_size ||
			     (e->metric_first_set &&
			      now_ms - e->metric_first_mono >= e->batch_ms) ||
			     (shutdown && e->metric_pending_count > 0)))
			{
				if (try_start_metric_post(e) == OTLP_OK)
					work_done = true;
			}
			/* If neither started, try log. */
			if (!e->in_flight && !e->backoff_armed &&
			    (e->log_pending_count >= e->batch_size ||
			     (e->log_first_set &&
			      now_ms - e->log_first_mono >= e->batch_ms) ||
			     (shutdown && e->log_pending_count > 0)))
			{
				if (try_start_log_post(e) == OTLP_OK)
					work_done = true;
			}
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
				/* On DONE, try to keep the socket alive for
				 * reuse on the next POST. _detach returns NULL
				 * if the response was Connection: close or the
				 * request wasn't in DONE state — in those cases
				 * _free will close it. */
				if (s == OTLP_HTTP_REQ_DONE)
					e->keepalive_sock =
						otlp_http_request_detach_socket(
							e->in_flight);
				record_outcome(e, status);
				otlp_http_request_free(e->in_flight);
				e->in_flight = NULL;
				e->in_flight_count = 0;
				/* If a network/server error happened and we
				 * captured a socket, drop it (it may be in a
				 * half-closed or inconsistent state). */
				if (status != 0 &&
				    (status < 200 || status >= 300) &&
				    e->keepalive_sock)
				{
					otlp_socket_close(e->keepalive_sock);
					e->keepalive_sock = NULL;
				}
			}
			work_done = true;
			(void) st;
		}

		/* 4. Backoff timer. The pending batch is retained
		 * across retries (freed in record_outcome on success
		 * or permanent failure); re-encode + retry now. The
		 * retry path dispatches based on which signal was last
		 * in-flight (record_outcome arms backoff only for
		 * transient failures, which retain the batch). */
		if (e->backoff_armed && !e->in_flight &&
			now_mono_ms() >= e->backoff_deadline_mono)
		{
			e->backoff_armed = false;
			switch (e->in_flight_signal)
			{
				case SIGNAL_SPAN:
					if (try_start_post(e) == OTLP_OK && e->in_flight)
						work_done = true;
					break;
				case SIGNAL_METRIC:
					if (try_start_metric_post(e) == OTLP_OK &&
					    e->in_flight)
						work_done = true;
					break;
				case SIGNAL_LOG:
					if (try_start_log_post(e) == OTLP_OK &&
					    e->in_flight)
						work_done = true;
					break;
			}
		}

		/* 5. If nothing else to do but we're waiting on backoff,
		 * sleep briefly so the tick actually advances time toward
		 * the deadline instead of returning immediately. */
		if (!work_done && e->backoff_armed && !e->in_flight)
		{
#if defined(_WIN32)
			Sleep(1); /* 1ms */
#else
			struct timespec ts = { 0, 1 * 1000 * 1000 /* 1ms */ };
			nanosleep(&ts, NULL);
#endif
			work_done = true;
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

	if (e->pending_count > 0 || e->in_flight ||
	    e->metric_pending_count > 0 || e->log_pending_count > 0)
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
	if (e) {
		e->null_transport_status_fn  = fn;
		e->null_transport_status_ctx = ctx;
	}
}

void
otlp_exporter_set_logger(otlp_exporter_t *e, otlp_log_fn fn, void *ctx)
{
	if (e) {
		e->log_fn  = fn;
		e->log_ctx = ctx;
	}
}

/* ── Synchronous metric / log flush ───────────────────────────── */

static otlp_status_t
flush_sync(struct otlp_exporter *e,
	   const char		       *path,
	   const uint8_t	       *body,
	   size_t			body_len)
{
	struct otlp_http_url url;
	otlp_http_request_t      *req = NULL;
	otlp_status_t		st;
	uint64_t			deadline;
	uint64_t			now;

	if (!e || !path || (!body && body_len > 0))
		return OTLP_ERR_NULL;
	if (e->null_transport)
		return OTLP_OK;
	url = e->url;
	snprintf(url.path, sizeof(url.path), "%s", path);
	st = otlp_http_request_start(&req, &url, e->user_agent, body, body_len,
				      e->connect_timeout_ms, e->read_timeout_ms);
	if (st != OTLP_OK)
		return st;
	deadline = now_mono_ms() + e->flush_timeout_ms;
	for (;;) {
		st = otlp_http_request_step(req);
		otlp_http_req_state_t s = otlp_http_request_state(req);

		if (s == OTLP_HTTP_REQ_DONE) {
			int http = otlp_http_request_http_status(req);

			otlp_http_request_free(req);
			if (http >= 200 && http < 300)
				return OTLP_OK;
			return OTLP_ERR_NETWORK;
		}
		if (s == OTLP_HTTP_REQ_FAILED) {
			otlp_http_request_free(req);
			return OTLP_ERR_NETWORK;
		}
		if (st != OTLP_OK && st != OTLP_ERR_WOULDBLOCK) {
			otlp_http_request_free(req);
			return st;
		}
		now = now_mono_ms();
		if (now >= deadline) {
			otlp_http_request_free(req);
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

otlp_status_t
otlp_exporter_flush_metric(otlp_exporter_t *e, const otlp_metric_t *m)
{
	struct otlp_pb_buf body = { 0 };
	otlp_status_t     st;
	const otlp_metric_t *arr[1];

	if (!e || !m)
		return OTLP_ERR_NULL;
	st = otlp_pb_buf_init(&body, 0);
	if (st != OTLP_OK)
		return st;
	arr[0] = m;
	st = otlp_encode_export_metrics_service_request(
		&body, e->service_name,
		e->resource_attributes, e->n_resource_attributes,
		NULL, NULL, arr, 1);
	if (st == OTLP_OK)
		st = flush_sync(e, "/v1/metrics", body.data, body.len);
	otlp_pb_buf_free(&body);
	return st;
}

otlp_status_t
otlp_exporter_flush_log(otlp_exporter_t *e, const otlp_log_record_t *lr)
{
	struct otlp_pb_buf body = { 0 };
	otlp_status_t     st;
	const otlp_log_record_t *arr[1];

	if (!e || !lr)
		return OTLP_ERR_NULL;
	st = otlp_pb_buf_init(&body, 0);
	if (st != OTLP_OK)
		return st;
	arr[0] = lr;
	st = otlp_encode_export_logs_service_request(
		&body, e->service_name,
		e->resource_attributes, e->n_resource_attributes,
		NULL, NULL, arr, 1);
	if (st == OTLP_OK)
		st = flush_sync(e, "/v1/logs", body.data, body.len);
	otlp_pb_buf_free(&body);
	return st;
}

otlp_status_t
otlp_exporter_shutdown(otlp_exporter_t *e)
{
	if (!e)
		return OTLP_ERR_NULL;
	otlp_atomic_store_int(&e->shutdown_requested, 1, OTLP_MEMORY_ORDER_RELEASE);
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
	out->emitted = otlp_atomic_load_u64(&e->emitted, OTLP_MEMORY_ORDER_RELAXED);
	out->dropped_full =
		otlp_atomic_load_u64(&e->dropped_full, OTLP_MEMORY_ORDER_RELAXED);
	out->dropped_err =
		otlp_atomic_load_u64(&e->dropped_err, OTLP_MEMORY_ORDER_RELAXED);
	out->sent = otlp_atomic_load_u64(&e->sent, OTLP_MEMORY_ORDER_RELAXED);
	out->http_2xx =
		otlp_atomic_load_u64(&e->http_2xx, OTLP_MEMORY_ORDER_RELAXED);
	out->http_4xx =
		otlp_atomic_load_u64(&e->http_4xx, OTLP_MEMORY_ORDER_RELAXED);
	out->http_5xx =
		otlp_atomic_load_u64(&e->http_5xx, OTLP_MEMORY_ORDER_RELAXED);
	out->network_err =
		otlp_atomic_load_u64(&e->network_err, OTLP_MEMORY_ORDER_RELAXED);
	out->emitted_metrics =
		otlp_atomic_load_u64(&e->emitted_metrics, OTLP_MEMORY_ORDER_RELAXED);
	out->sent_metrics =
		otlp_atomic_load_u64(&e->sent_metrics, OTLP_MEMORY_ORDER_RELAXED);
	out->dropped_metrics_full =
		otlp_atomic_load_u64(&e->dropped_metrics_full, OTLP_MEMORY_ORDER_RELAXED);
	out->dropped_metrics_err =
		otlp_atomic_load_u64(&e->dropped_metrics_err, OTLP_MEMORY_ORDER_RELAXED);
	out->emitted_logs =
		otlp_atomic_load_u64(&e->emitted_logs, OTLP_MEMORY_ORDER_RELAXED);
	out->sent_logs =
		otlp_atomic_load_u64(&e->sent_logs, OTLP_MEMORY_ORDER_RELAXED);
	out->dropped_logs_full =
		otlp_atomic_load_u64(&e->dropped_logs_full, OTLP_MEMORY_ORDER_RELAXED);
	out->dropped_logs_err =
		otlp_atomic_load_u64(&e->dropped_logs_err, OTLP_MEMORY_ORDER_RELAXED);
	return OTLP_OK;
}
