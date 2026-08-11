# TODO 86 — Table-driven exporter free-drain + span clone-shutdown test

**Status:** Complete (v0.5.46)
**Priority:** P3 (architecture: closes per-signal triplication;
test symmetry)

## What shipped

Two related cleanups bundled:

1. **Free-drain descriptor.** `otlp_exporter_free` had the last
   per-signal triplication in the exporter: three
   `while (mpsc_queue_pop(...)) free(...)` loops followed by three
   `for (i ...) free(pending[i])` loops. Replaced with a
   `signal_drain_path` descriptor + single `drain_signal` helper.
   Adding a 4th signal is one entry in a 3-element array.

2. **Orphan cleanup.** The `free_pending_batch` helper had a
   single caller left after v0.5.44 refactored the other away.
   Removed.

3. **Test symmetry.** v0.5.42 added `prop_async_metric_clone_shutdown`
   and `prop_async_log_clone_shutdown` but no span equivalent.
   `prop_async_span_clone_shutdown` now exists — locks in that
   all three clone variants return SHUTDOWN without leaking.

## Sites changed

- `src/exporter.c`:
  - Promoted `span_free_void`, `metric_free_void`, `log_free_void`
    to the lifecycle section (needed by the drain code there).
  - Added `struct signal_drain_path`.
  - Added `drain_signal` helper.
  - Rewrote `otlp_exporter_free`'s drain section to use the
    descriptor in a loop.
  - Removed `free_pending_batch` (orphan).
- `tests/property/test_property_async_metrics.c`:
  - Added `prop_async_span_clone_shutdown`.

## Why

- **DRY.** The drain loop has one owner (`drain_signal`), not
  three pairs of inline loops.
- **OCP.** Adding a 4th signal = one entry in the `drains[3]` array
  (becomes `drains[4]`), not a copy-paste of two loops.
- **MECE.** The exporter's per-signal triplication story is now
  fully closed: emit, record_outcome, start_post, AND free-drain
  all use descriptors.
- **Test symmetry.** All three clone variants now have shutdown
  regression coverage.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 40/40 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

The async_metrics properties exercise all three signals through
the free-drain path (every property ends with exporter_free). A
miswired drain descriptor would surface as ASAN leaks at free.

## Remaining per-signal pattern

The only remaining inline-per-signal pattern in the exporter is
`flush_metric` / `flush_log`. These are 2-way (no `flush_span` —
spans use the async queue exclusively) and have complex encode-fn
signatures. Refactoring them to a descriptor requires void
wrappers around the typed encode functions; the win is small for
a 2-way pattern. Deferred until a 3rd synchronous flush signal
appears or another reason forces the refactor.
