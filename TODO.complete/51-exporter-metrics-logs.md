# TODO 51 — Exporter metrics/logs support

**Status:** Complete (v0.5.9)
**Priority:** P1
**Depends on:** 20, 21

## Goal

Export metrics and logs via the exporter. Previously only traces
could be exported; metrics/logs could be encoded to wire bytes but
had no send path.

## What shipped (v0.5.9)

**Public API** (`include/otlp-c/exporter.h`):
- `otlp_exporter_flush_metric(exp, metric)` — synchronously encodes
  and POSTs one metric to `/v1/metrics`.
- `otlp_exporter_flush_log(exp, log)` — synchronously encodes and
  POSTs one log record to `/v1/logs`.

**Implementation** (`src/exporter.c`):
- `flush_sync` internal helper: encodes body, opens HTTP request to
  the given path, drives the state machine to completion (blocking
  with 1ms poll), returns success/failure.
- URL is derived from the exporter's configured endpoint by replacing
  the path component (`/v1/traces` → `/v1/metrics` or `/v1/logs`).
- Null-transport mode: returns OK immediately (no HTTP).

**Example** (`examples/minimal.c`):
- Updated to demonstrate the full v0.5.x API: span with attributes +
  events, metric counter, log record, context propagation, sampler.

## Design notes

Synchronous flush is chosen over async batching for metrics/logs
because:
- Metrics/logs are typically low-frequency (periodic gauges, shutdown
  counters, error logs) compared to high-frequency traces.
- Batching metrics/logs requires signal-type-aware queues, which
  significantly complicates the exporter's tick loop.
- Synchronous flush reuses the existing HTTP infrastructure
  (`http_client.c`) without changes.

Async batching for metrics/logs is a future enhancement (TODO 52).

## Acceptance criteria
- [x] CI green on all platforms.
- [x] No regression in existing tests (26/26 pass).
- [x] Example demonstrates metric + log + span + context.

## Out of scope (deferred)
- Async batching for metrics/logs (separate MPSC queue per signal).
- Retry with backoff for metric/log flush (currently single attempt).
- Keepalive socket reuse for metric/log flush (fresh connection each).
