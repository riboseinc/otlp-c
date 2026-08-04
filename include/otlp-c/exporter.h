/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Exporter — batches spans and POSTs them to an OTLP collector.
 *
 * Lifetime: caller-owned. Construct via otlp_exporter_create();
 * free via otlp_exporter_free() (call shutdown first if you want
 * any pending batch to flush).
 *
 * Thread-safety: thread-safe at the API level. The exporter holds
 * a mutex around batch emission.
 *
 * Flow:
 *   1. Caller calls otlp_exporter_emit() once per span.
 *   2. Exporter buffers spans until batch_size or batch_ms triggers.
 *   3. Background thread encodes the batch and POSTs it to the
 *      configured endpoint.
 *   4. Retry with exponential backoff on transient errors.
 */
#ifndef OTLP_C_EXPORTER_H
#define OTLP_C_EXPORTER_H

#include <stddef.h>
#include <stdint.h>

#include "span.h"
#include "status.h"
#include "visibility.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct otlp_exporter otlp_exporter_t;

/* Configuration for otlp_exporter_create. Pass zero-initialized +
 * fill the fields you care about; the library supplies defaults
 * for the rest. */
typedef struct {
	/* OTLP/HTTP endpoint. Default: "http://localhost:4318/v1/traces".
	 * Must include scheme + host + port + path. */
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
} otlp_exporter_opts_t;

/* Construct an exporter. The opts are copied; the caller may free
 * them after this returns. Returns NULL on allocation failure.
 *
 * The constructor does NOT open a socket. The first emit triggers
 * a background thread that owns the lifetime of network state.
 */
OTLP_C_EXPORT
otlp_exporter_t *otlp_exporter_create(const otlp_exporter_opts_t *opts);

OTLP_C_EXPORT
void otlp_exporter_free(otlp_exporter_t *exp);

/* Add a span to the current batch. Returns immediately; the
 * exporter copies the span internally. The caller may free or
 * reuse the span argument after this returns.
 *
 * Returns:
 *   OTLP_OK on success.
 *   OTLP_ERR_NULL if exp or span is NULL.
 *   OTLP_ERR_BUFFER_FULL if the internal queue is full and the
 *     drop policy is set to drop (default). The dropped counter
 *     is incremented.
 *   OTLP_ERR_SHUTDOWN if otlp_exporter_shutdown was called.
 */
OTLP_C_EXPORT
otlp_status_t otlp_exporter_emit(otlp_exporter_t *exp,
				 const otlp_span_t *span);

/* Flush any pending spans synchronously. Blocks until either the
 * batch has been successfully POSTed or the retry budget is
 * exhausted. Use at clean shutdown.
 *
 * Returns:
 *   OTLP_OK on success.
 *   OTLP_ERR_* on the last failure.
 */
OTLP_C_EXPORT
otlp_status_t otlp_exporter_flush(otlp_exporter_t *exp);

/* Signal that the exporter should stop accepting new spans.
 * Pending spans are flushed in the background. Subsequent emit()
 * calls return OTLP_ERR_SHUTDOWN. The exporter is still owned by
 * the caller and must be freed with otlp_exporter_free().
 *
 * Returns immediately. Use otlp_exporter_flush() to wait for
 * pending spans to drain.
 */
OTLP_C_EXPORT
otlp_status_t otlp_exporter_shutdown(otlp_exporter_t *exp);

/* Diagnostic counters. All monotonically increasing. */
typedef struct {
	uint64_t emitted;       /* spans accepted by emit() */
	uint64_t dropped_full;  /* spans dropped because buffer was full */
	uint64_t dropped_err;   /* spans dropped after max_retries */
	uint64_t sent;          /* spans successfully POSTed */
	uint64_t http_2xx;      /* HTTP responses in 2xx */
	uint64_t http_4xx;      /* HTTP responses in 4xx */
	uint64_t http_5xx;      /* HTTP responses in 5xx */
	uint64_t network_err;   /* network failures before HTTP */
} otlp_exporter_stats_t;

OTLP_C_EXPORT
otlp_status_t otlp_exporter_get_stats(otlp_exporter_t *exp,
				      otlp_exporter_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
