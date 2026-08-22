# TODO 140 — Structured diagnostics events (diagnostics as data)

**Status:** Complete (v0.5.100)
**Priority:** P2 (production-observability feature; deferred since v0.5.96)

## The gap

`otlp_exporter_set_logger()` (v0.5.23) delivered diagnostics as
formatted strings. Fine for humans; useless for programs — a
consumer wanting "alert when items are dropped, grouped by signal
and reason" had to parse printf output. The 14 diagnostic sites
each hand-rolled their own format string, so the facts (signal,
count, status, attempt, delay) were encoded in prose, redundantly,
and the sync path carried a `path + 5` string trick to recover
the signal name from the URL path.

## What shipped

**Public API** (`include/otlp-c/exporter.h`, additive — OCP):

- `otlp_signal_id_t` (TRACES/METRICS/LOGS), `otlp_drop_reason_t`
  (MAX_RETRIES/HTTP_STATUS/QUEUE_FULL), `otlp_event_code_t`
  (QUEUE_FULL/BATCH_SENT/RETRY_ARMED/ITEMS_DROPPED/
  PARTIAL_SUCCESS/SYNC_FLUSH_FAILED), `otlp_event_t` (code,
  level, signal, count, rejected, http_status, attempt,
  max_retries, delay_ms, timeout_ms, status, drop_reason,
  server_driven, detail+detail_len), `otlp_event_fn`,
  `otlp_exporter_set_event_logger()`. Same thread-safety contract
  as the string logger.

**Single model, derived presentation** (`src/exporter.c`):
every diagnostic site now builds one `otlp_event_t` and calls
`event_log()`, which dispatches to the structured callback and —
if the string logger is installed — renders the message with
`format_event()`, the ONE formatter (a switch over event codes).
Consequences:

- the two views cannot diverge (the string IS a function of the
  event);
- DRY: 14 printf sites and their duplicated facts collapse into
  the event constructions + one presentation point;
- the variadic `otlp_log()` helper is deleted;
- signal names derive from the event's signal id, unifying the
  old span/spans, metric/metrics wording inconsistencies;
- the emit/record path descriptors carry `otlp_signal_id_t`
  instead of name strings; the sync-flush path takes the signal
  as a parameter — `path + 5` is gone.

**Tests**:

- `tests/test_exporter_events.c` (7 scenarios, null transport,
  deterministic): BATCH_SENT per signal id, RETRY_ARMED
  (attempt/delay/status/level), ITEMS_DROPPED for max-retries and
  permanent-4xx with distinct drop reasons, QUEUE_FULL firing on
  the emitting thread (count matches BUFFER_FULL returns),
  SYNC_FLUSH_FAILED on one-shot flush to a dead port.
- PartialSuccess wire test extended: asserts the
  PARTIAL_SUCCESS event (signal, count, rejected, bounded detail
  copy) on spans AND logs, and that a clean 200 emits no event.

Two test defects surfaced during validation and were fixed:

- the queue-full scenario used `emit()` (clone variant) and never
  freed the caller-owned originals — a real leak macOS leak-check
  caught (switched to `emit_move`; the library owns the span on
  every path there);
- `assert(ev->delay_ms > 0)` on the first retry event flaked ~40%
  of runs: retry delay is FULL jitter (uniform [0, base]), so 0
  is a legitimate draw — ~50% likely with 1ms test bounds. Not a
  race; the assert now bounds delay by backoff_max. Lesson added
  to the test-writing memory.

## Verification

- Debug (30 consecutive runs of the new suite) + Release/ASAN
  (15 runs, leak detection on)/UBSAN/TSAN green — 44/44; fresh
  Release tree: zero warnings.

## Remaining work

- Golden vectors (payload-level encode validation) — still open
  from TODO 139.
- The examples could demonstrate the event callback alongside the
  string logger (docs polish).
