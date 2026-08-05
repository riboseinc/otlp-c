/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Exporter — batches spans and POSTs them to an OTLP collector.
 *
 * Lifetime: caller-owned. Construct via otlp_exporter_create();
 * free via otlp_exporter_free() (call shutdown first if you want
 * any pending batch to flush).
 *
 * Caller-driven I/O: the library never spawns threads. The caller
 * drives progress by calling otlp_exporter_tick() from a thread it
 * controls — its event loop, a periodic timer, a worker, etc. See
 * docs/deployment.md for embedding patterns.
 *
 * Thread-safety: emit() is safe to call from any thread. tick(),
 * flush(), shutdown(), free(), and get_stats() are NOT — the caller
 * must serialise them (typically by always calling from the same
 * thread that owns the exporter's lifetime).
 *
 * Flow:
 *   1. Caller calls otlp_exporter_emit() once per span from any
 *      thread. Spans are deep-copied into a lock-free MPSC queue.
 *   2. Caller calls otlp_exporter_tick() to drain the queue, encode
 *      a batch, and drive the in-flight HTTP POST.
 *   3. Retry with exponential backoff on transient errors.
 */
#ifndef OTLP_C_EXPORTER_H
#define OTLP_C_EXPORTER_H

#include <stddef.h>
#include <stdint.h>

#include "span.h"
#include "status.h"
#include "visibility.h"

#ifdef __cplusplus
extern "C"
{
#endif

	typedef struct otlp_exporter otlp_exporter_t;

	/* Configuration for otlp_exporter_create. Pass zero-initialized +
	 * fill the fields you care about; the library supplies defaults
	 * for the rest. */
	typedef struct
	{
		/* OTLP/HTTP endpoint. Default:
		 * "http://localhost:4318/v1/traces". Must include scheme + host
		 * + port + path. The library talks plain HTTP to localhost; the
		 * otelcol sidecar terminates TLS to the real backend. See
		 * docs/deployment.md. */
		const char *endpoint;

		/* Service name attached to every batch's Resource. Default: "".
		 * Override per-tracer if you need different service names for
		 * different spans. */
		const char *service_name;

		/* Max spans per HTTP request. Default: 512. */
		size_t batch_size;

		/* Max milliseconds to wait before flushing a partial batch.
		 * Default: 100. */
		uint32_t batch_ms;

		/* Max retry attempts on transient errors (429, 5xx, network).
		 * Default: 5. */
		uint32_t max_retries;

		/* Initial backoff in milliseconds. Default: 1000 (1s). */
		uint32_t backoff_initial_ms;

		/* Max backoff in milliseconds. Default: 30000 (30s). */
		uint32_t backoff_max_ms;

		/* Connect timeout in milliseconds. Default: 5000 (5s). */
		uint32_t connect_timeout_ms;

		/* Read timeout in milliseconds. Default: 10000 (10s). */
		uint32_t read_timeout_ms;

		/* User-Agent header. Default: "otlp-c/<version>". */
		const char *user_agent;

		/* MPSC queue capacity (must be power of 2). Default: 4096. */
		size_t queue_capacity;
	} otlp_exporter_opts_t;

	/* Construct an exporter. The opts are copied; the caller may free
	 * them after this returns. Returns NULL on allocation failure.
	 *
	 * The constructor does NOT open a socket. The first tick() after
	 * emit() triggers the network I/O. */
	OTLP_C_EXPORT
	otlp_exporter_t *otlp_exporter_create(const otlp_exporter_opts_t *opts);

	OTLP_C_EXPORT
	void otlp_exporter_free(otlp_exporter_t *exp);

	/* Add a span to the queue. Safe to call from any thread.
	 * The span is deep-copied; the caller may free or reuse it
	 * immediately.
	 *
	 * Returns:
	 *   OTLP_OK on success.
	 *   OTLP_ERR_NULL if exp or span is NULL.
	 *   OTLP_ERR_BUFFER_FULL if the MPSC queue is at capacity.
	 *     emitted is NOT incremented; dropped_full is.
	 *   OTLP_ERR_SHUTDOWN if otlp_exporter_shutdown was called.
	 */
	OTLP_C_EXPORT
	otlp_status_t otlp_exporter_emit(otlp_exporter_t *exp,
		const otlp_span_t *span);

	/* Drive exporter progress by one step. Drains the queue into a
	 * pending batch, starts or advances the in-flight HTTP request,
	 * fires batch/backoff timers. Returns when there is nothing left
	 * to do OR when max_wait_ms elapses (whichever comes first).
	 *
	 * THREAD-SAFETY: NOT safe to call concurrently from multiple
	 * threads. Pick one thread (your event loop / worker / main) and
	 * call tick from there.
	 *
	 * Returns OTLP_OK on every iteration, including when WOULDBLOCK
	 * would have been returned internally — the caller does not need
	 * to differentiate. Check get_stats() for outcome counters. */
	OTLP_C_EXPORT
	otlp_status_t otlp_exporter_tick(otlp_exporter_t *exp,
		uint32_t max_wait_ms);

	/* Flush any pending spans synchronously. Blocks the calling
	 * thread by internally looping tick() until the queue is empty
	 * and no request is in flight, or until the retry budget is
	 * exhausted. Use at clean shutdown.
	 *
	 * Returns:
	 *   OTLP_OK on success.
	 *   OTLP_ERR_* on the last failure. */
	OTLP_C_EXPORT
	otlp_status_t otlp_exporter_flush(otlp_exporter_t *exp);

	/* Signal that the exporter should stop accepting new spans.
	 * Subsequent emit() calls return OTLP_ERR_SHUTDOWN. The exporter
	 * is still owned by the caller and must be freed with
	 * otlp_exporter_free(). Pending spans are NOT auto-flushed;
	 * call tick() (or flush()) to drain them. */
	OTLP_C_EXPORT
	otlp_status_t otlp_exporter_shutdown(otlp_exporter_t *exp);

	/* Poll-fd descriptor for event-loop integration. events is
	 * POLLIN=1, POLLOUT=2 (matches <poll.h>). */
	typedef struct
	{
		int fd;
		int events;
	} otlp_poll_fd_t;

	/* Get fds + interest bits the caller should register in its event
	 * loop. Returns 0 fds if there's no in-flight request; the caller
	 * should still call tick() periodically (at least every batch_ms)
	 * to drain the queue and start new requests.
	 *
	 * THREAD-SAFETY: same as tick(). */
	OTLP_C_EXPORT
	otlp_status_t otlp_exporter_poll_fds(otlp_exporter_t *exp,
		otlp_poll_fd_t *out,
		size_t cap,
		size_t *n_out);

	/* Diagnostic counters. All monotonically increasing. */
	typedef struct
	{
		uint64_t emitted; /* spans accepted by emit() */
		uint64_t
			dropped_full; /* spans dropped because queue was full */
		uint64_t dropped_err; /* spans dropped after max_retries */
		uint64_t sent; /* spans successfully POSTed */
		uint64_t http_2xx; /* HTTP responses in 2xx */
		uint64_t http_4xx; /* HTTP responses in 4xx */
		uint64_t http_5xx; /* HTTP responses in 5xx */
		uint64_t network_err; /* network failures before HTTP */
	} otlp_exporter_stats_t;

	OTLP_C_EXPORT
	otlp_status_t otlp_exporter_get_stats(otlp_exporter_t *exp,
		otlp_exporter_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
