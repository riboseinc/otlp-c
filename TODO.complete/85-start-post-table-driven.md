# TODO 85 — Table-driven start-post pipeline

**Status:** Complete (v0.5.45)
**Priority:** P2 (architecture: DRY + OCP, completes the per-signal
dispatch trilogy)

## What shipped

The three start-post functions (`try_start_post`,
`try_start_metric_post`, `try_start_log_post`) were near-identical
copies. Each had the same shape: NULL/empty check → call the
type-specific `otlp_exporter_otel_build_*_request` → on failure
clear keepalive_sock → on success populate in_flight state.

Replaced with a descriptor pattern matching v0.5.43 (emit) and
v0.5.44 (record_outcome):

- `struct signal_start_path` — per-signal descriptor: pending
  array (type-erased), pending_count, first_set pointer,
  signal_kind, build_request fn pointer.
- `try_start_post_common(e, &path)` — single owner of the
  NULL/empty check, build call, keepalive handling, and
  in_flight state population.
- Three thin wrappers that build a descriptor and delegate.
- Three `build_*_request_void` wrappers that type-erase the items
  parameter so all three typed build helpers fit one
  function-pointer signature.

## Sites changed

- `src/exporter.c`:
  - Added `struct signal_start_path`.
  - Added `build_span_request_void`, `build_metric_request_void`,
    `build_log_request_void` (type-erased wrappers around the
    typed `otlp_exporter_otel_build_*` functions).
  - Added `try_start_post_common`.
  - Rewrote `try_start_post`, `try_start_metric_post`,
    `try_start_log_post` as thin descriptor-driven wrappers.

## Why

- **DRY.** Behavior changes touch one helper, not three.
- **OCP.** Adding a 4th signal = one descriptor + one `*_void`
  wrapper.
- **MECE.** Single owner of the start-post pipeline. The
  typed-vs-type-erased boundary is explicit: typed at the
  `exporter_otel` boundary, erased inside `exporter.c`.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 39/39 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

The async_metrics properties exercise all three signals through
the start_post path (batch ready → POST → response). A miswired
descriptor would surface as wrong-signal POSTs or wrong
in_flight state.

## Per-signal dispatch trilogy complete

This release finishes the per-signal dispatch trilogy:
- v0.5.43: emit pipeline (descriptor + clone/move helpers).
- v0.5.44: record_outcome (descriptor + outcome helpers).
- v0.5.45: start_post (descriptor + build helpers).

The exporter's per-signal triplication is now fully eliminated.
Adding a 4th signal is three descriptors + three wrappers, no
core-logic changes.

## Future

The remaining per-signal patterns are:
- `exporter_free`'s queue-drain + pending-batch-free (still
  inline per signal; one-shot at shutdown, low impact).
- `flush_metric` / `flush_log` (still inline encode + sync POST;
  only 2-way not 3-way since spans use the async pipeline).

Each is a candidate for a follow-up. The exporter's hot paths
(emit, tick, record_outcome, start_post) are now table-driven;
the remaining patterns are cold paths.
