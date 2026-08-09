# TODO 75 — Fix null_transport backoff-retry + metric/log retry tests

**Status:** Complete (v0.5.35)
**Priority:** P0 (correctness fix) + P1 (test coverage)

## What shipped (v0.5.35)

### Fix: backoff retry starts HTTP under null_transport

The backoff retry path in tick() (step 5, the `paths[in_flight_signal].start_post(e)` call) unconditionally started an HTTP request, even when null_transport was enabled. This caused double-processing:

1. Backoff retry starts HTTP request (step 5).
2. Next tick iteration: null-transport fast path (step 2) sees pending data and re-sends via null_transport.
3. Both paths call record_outcome → sent_metrics increments twice for one emit.

Fix: added `!e->null_transport` check. When null_transport is enabled, backoff is cleared but no HTTP starts. The next tick's null-transport path handles the retry cleanly.

### Added: metric/log retry property tests

- `prop_async_metric_retry` — 500 → retry → 200. Verifies sent_metrics == 1 (not 2).
- `prop_async_log_retry` — 503 → retry → 200. Verifies sent_logs == 1.

## Acceptance criteria
- [x] Backoff retry skips HTTP when null_transport is enabled.
- [x] Metric retry: 500 → 200 → sent_metrics == 1.
- [x] Log retry: 503 → 200 → sent_logs == 1.
- [x] 34/34 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
