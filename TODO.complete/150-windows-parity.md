# TODO 150 — Windows test parity

**Status:** Complete (v0.6.4)
**Priority:** P3 (CI matrix parity)

## The gap

`exporter-retry` was fully portable (null transport, no threads,
no clocks — its own header even said "runs on all platforms")
but sat in the `if(UNIX)` CMake block, so the Windows CI job
never ran it. `exporter-events` was portable except for its
wall-clock drive loop (`clock_gettime` + `CLOCK_MONOTONIC` —
the latter doesn't exist on MSVC).

## What shipped

- `exporter-events` now drives on OUTCOME, not wall time:
  `drive_until(exp, want_event, want_count, max_iters)` ticks
  (each tick sleeps ≤5ms via the library's own portable sleep)
  until the expected event count arrives. This is not the
  flaky "iteration-count-bounded timeout wait" pattern — the
  loop exits on the scenario's terminal outcome; the iteration
  budget (~1s at 5ms/tick) is far past these scenarios' 5ms
  backoff caps. The `_POSIX_C_SOURCE` guard and `<time.h>` are
  gone.
- Both tests moved out of the `if(UNIX)` block; the Windows CI
  job now runs the retry state machine (full backoff/retry/drop
  coverage) and the structured-events suite.

## Verification

49/49 in all five local configurations; events stable across
repeated runs; fresh Release tree zero warnings. The Windows CI
job is the authoritative gate for MSVC compilation of the moved
tests (ran green on the PR).
