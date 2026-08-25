/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Exporter internals — internal-only header, NOT installed. The
 * public surface is include/otlp-c/exporter.h. This header is the
 * ONE internal seam of the exporter family (v1.1.2): the struct
 * lives here so exporter.c (lifecycle + async tick pipeline) and
 * exporter_sync.c (synchronous flush delivery) share it without
 * either reaching into the other's file.
 */
#ifndef OTLP_C_EXPORTER_INTERNAL_H
#define OTLP_C_EXPORTER_INTERNAL_H

#include <otlp-c/exporter.h>

#include "atomic_compat.h"
#include "internal_util.h"
#include "mpsc_queue.h"
#include "http_client.h"
#include "span_internal.h" /* struct otlp_attr_vec */

#include <stddef.h>
#include <stdint.h>

/* Signal kind constants — also the index into sig[] and
 * SIGNAL_SPECS[]. Matches otlp_signal_id_t's values. */
enum
{
	SIGNAL_SPAN = 0,
	SIGNAL_METRIC = 1,
	SIGNAL_LOG = 2,
	N_SIGNALS
};

/* Per-signal runtime state (one instance per signal, sig[3]).
 * The static-constant half of the descriptor (name, free/clone/
 * build adapters) lives in SIGNAL_SPECS below. */
struct signal_state
{
	/* Per-signal endpoint (v0.7.3): derived at create from the
	 * endpoint base + the spec's default_path, or set from the
	 * signal-specific override. Immutable after create. */
	struct otlp_http_url url;
	struct mpsc_queue queue;
	void **pending;
	size_t pending_cap;
	size_t pending_count;
	bool first_set;
	uint64_t first_mono;
	otlp_atomic_u64 emitted;
	otlp_atomic_u64 dropped_full;
	otlp_atomic_u64 dropped_err;
	otlp_atomic_u64 sent;
	otlp_atomic_u64 rejected;
};

struct otlp_exporter
{
	/* Immutable after create. (Per-signal endpoints live in
	 * sig[s].url.) */
	char *user_agent;
	char *service_name;
	char *schema_url;
	struct otlp_attr_vec resource_attrs;
	size_t batch_size;
	uint32_t batch_ms;
	uint32_t max_retries;
	uint32_t backoff_initial_ms;
	uint32_t backoff_max_ms;
	uint32_t flush_timeout_ms;
	uint32_t connect_timeout_ms;
	uint32_t read_timeout_ms;

	/* Per-signal state, indexed by signal kind (SIGNAL_SPAN=0,
	 * SIGNAL_METRIC=1, SIGNAL_LOG=2): queue, pending batch, batch
	 * timer, and the per-signal stats counters, ONE struct instead
	 * of fifteen hand-named fields per signal. Every signal-generic
	 * driver (emit, tick, record_outcome, start-post, drain, stats)
	 * indexes this array. */
	struct signal_state sig[3];

	/* HTTP-level counters (not per-signal). */
	otlp_atomic_u64 http_2xx;
	otlp_atomic_u64 http_4xx;
	otlp_atomic_u64 http_5xx;
	otlp_atomic_u64 network_err;
	otlp_atomic_int shutdown_requested;

	/* Optional diagnostic callbacks (NULL = no-op). The event
	 * callback receives the structured model; the string logger
	 * receives the message derived from it by format_event(). */
	otlp_log_fn log_fn;
	void *log_ctx;
	otlp_event_fn event_fn;
	void *event_ctx;

	/* In-flight request state. in_flight_signal identifies which
	 * signal's batch is being POSTed (0=span, 1=metric, 2=log). */
	otlp_http_request_t *in_flight;
	int in_flight_signal;
	size_t in_flight_count;
	uint32_t attempt;
	uint64_t backoff_deadline_mono;
	bool backoff_armed;
	/* Extra HTTP headers (v0.7.2): deep-copied from opts at
	 * create time; owned strings in one block. */
	otlp_http_header_t *http_headers;
	size_t n_http_headers;
	char *http_headers_blob;

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

/* Diagnostics dispatch: exporter.c owns the model + formatter;
 * exporter_sync.c reports through the same single entry. */
void otlp_exporter_event_log(const struct otlp_exporter *e,
	const otlp_event_t *ev);
void otlp_exporter_report_partial_success(struct otlp_exporter *e,
	otlp_signal_id_t signal,
	otlp_atomic_u64 *rejected_counter,
	uint64_t count,
	const uint8_t *body,
	size_t body_len);


/* Test-only: the exporter's copy of opts.resource_attributes AFTER
 * map-semantics normalization (duplicate keys collapsed
 * last-write-wins; "service.name" dropped when the dedicated
 * service_name opt is set). Points into the exporter; valid until
 * otlp_exporter_free(). */
const struct otlp_attribute *
otlp_exporter_get_resource_attrs(const otlp_exporter_t *e, size_t *n);

#endif
