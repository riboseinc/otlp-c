# TODO 68 — Async metric/log batching

**Status:** Complete (v0.5.28)
**Priority:** P0 (biggest architectural gap, now closed)

## The gap

Since the library's inception, traces and metrics/logs used
different pipelines:

| Signal | Pipeline | Blocking? |
|---|---|---|
| Traces | emit → MPSC queue → tick → batch → encode → POST | No (async) |
| Metrics | flush_metric → encode → POST → return | Yes (sync) |
| Logs | flush_log → encode → POST → return | Yes (sync) |

A caller emitting a metric from a hot path blocked on HTTP.
The library's #1 selling point — "non-blocking, caller-driven,
embeddable" — was undermined for two of the three signals.

This was the single biggest architectural gap in the project.

## The fix (v0.5.28)

### New public API (move semantics)

```c
otlp_status_t otlp_exporter_emit_metric_move(
    otlp_exporter_t *exp, otlp_metric_t *metric);

otlp_status_t otlp_exporter_emit_log_move(
    otlp_exporter_t *exp, otlp_log_record_t *log);
```

Move semantics: the caller gives up ownership. The exporter
frees the metric/log after encoding (or on drop). Same contract
as `otlp_exporter_emit_move` for spans.

### Architecture

The exporter now has THREE MPSC queues (span, metric, log).
`tick()` drains all three, batches per signal, and POSTs to the
correct endpoint:

```
tick():
    drain span_queue → span_pending
    drain metric_queue → metric_pending
    drain log_queue → log_pending

    if null_transport:
        try span → metric → log (priority)

    if !in_flight and !backoff:
        if span batch ready:   POST to /v1/traces
        elif metric batch ready: POST to /v1/metrics
        elif log batch ready:    POST to /v1/logs

    if in_flight: step()

    if backoff expired:
        retry the signal that was last in-flight
```

**Design decisions:**

- **One in-flight at a time.** The exporter drives ONE HTTP
  request at a time, shared across all signals. This keeps the
  HTTP state machine simple (one fd, one poll target). Multiple
  concurrent requests would require an fd-per-request poll set.
  Deferred.

- **Priority: span > metric > log.** Traces are the primary
  signal; metrics and logs are secondary. When multiple signals
  have ready batches, traces go first.

- **Shared backoff.** A failure on any signal arms backoff for
  ALL signals. This prevents hammering a broken collector.
  Per-signal backoff is a refinement for later.

- **Move-only for v0.5.28.** `emit_metric` (with deep-clone)
  is not provided. Callers who need to keep their metric can
  build a fresh one for each emit. A `otlp_metric_clone` +
  `emit_metric` convenience pair can be added if demand exists.

- **Same batch_size/batch_ms for all signals.** The opts have
  one `batch_size` and one `batch_ms`. All three signals use
  them. Callers needing per-signal batching can use separate
  exporters.

### record_outcome changes

`record_outcome` now dispatches batch-clearing and per-signal
stats based on `in_flight_signal`:

- `clear_in_flight_batch(e)` frees the correct pending array
  (span/metric/log) based on the signal.
- `add_sent_for_signal(e)` increments the correct `sent` counter.
- `add_dropped_err_for_signal(e)` increments the correct
  `dropped_err` counter.

HTTP-level counters (http_2xx, http_4xx, http_5xx, network_err)
remain global.

### flush() drains all signals

`otlp_exporter_flush()` now loops tick() until ALL three queues
are empty (not just the span queue). The flush-completion check
includes `metric_pending_count`, `log_pending_count`,
`mpsc_queue_size(&e->metric_queue)`, and `mpsc_queue_size(&e->log_queue)`.

### Per-signal stats

8 new fields on `otlp_exporter_stats_t`:

| Field | Description |
|---|---|
| `emitted_metrics` | metrics accepted by emit_metric_move |
| `sent_metrics` | metrics successfully POSTed |
| `dropped_metrics_full` | metrics dropped: queue full |
| `dropped_metrics_err` | metrics dropped: max retries |
| `emitted_logs` | logs accepted by emit_log_move |
| `sent_logs` | logs successfully POSTed |
| `dropped_logs_full` | logs dropped: queue full |
| `dropped_logs_err` | logs dropped: max retries |

Existing span counters unchanged (backward compatible).

### Property tests

`tests/property/test_property_async_metrics.c` (4 properties):
- `prop_async_metric_sent` — emit + tick + verify sent_metrics=1.
- `prop_async_log_sent` — same for logs.
- `prop_async_spans_coexist` — spans + metrics coexist.
- `prop_async_metric_drop_full` — queue overflow → BUFFER_FULL
  + dropped_metrics_full.

## Why this matters

This is the single most impactful change of the v0.5.x line.
Before v0.5.28, the library was "async for traces, sync for
everything else" — a fundamental inconsistency in a library
whose core value prop is non-blocking, caller-driven I/O.

Now all three signals flow through the same proven pipeline:
MPSC queue → tick drain → batch → encode → non-blocking POST →
retry with backoff. The caller's hot path never blocks on HTTP
for any signal.

## Acceptance criteria
- [x] `emit_metric_move` / `emit_log_move` public functions.
- [x] Metric/log MPSC queues in exporter struct.
- [x] tick() drains all three queues.
- [x] tick() POSTs to correct endpoint per signal.
- [x] Null-transport path handles all three signals.
- [x] Backoff retry dispatches per signal.
- [x] flush() drains all signals.
- [x] Per-signal stats (8 new fields).
- [x] Span path structurally unchanged (all existing tests pass).
- [x] 33/33 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
