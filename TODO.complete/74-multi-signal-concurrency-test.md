# TODO 74 — Multi-signal concurrency stress test

**Status:** Complete (v0.5.34)
**Priority:** P1 (race validation)

## What shipped (v0.5.34)

### Multi-signal concurrency stress test

`tests/test_concurrency_stress_multi.c` — validates the v0.5.28
async metric/log pipeline under concurrent load.

**Test design:**
- 8 worker threads, each emitting 100 items PER SIGNAL:
  - 100 spans via `emit()` (clone + MPSC push)
  - 100 metrics via `emit_metric_move()` (MPSC push)
  - 100 logs via `emit_log_move()` (MPSC push)
- Main thread drives progress via `flush()` (which loops tick).
- Uses null_transport for determinism (no echo server threads).

**Verifies:**
- Per-signal emit counts: 800 spans, 800 metrics, 800 logs.
- Per-signal sent counts: all 800 each.
- No lost items (emit == sent for each signal).
- No race conditions (TSAN-clean).

**Why this matters:**

The v0.5.28 async metric/log pipeline introduced three concurrent
MPSC queues, shared in-flight state, and table-driven signal
dispatch (v0.5.30 DRY refactor). Until v0.5.34, this architecture
was only tested with:
- Property tests (single-threaded emit + tick via null_transport)
- The existing span-only concurrency stress test

The three-queue, one-in-flight, shared-backoff design had never
been stress-tested with ALL THREE SIGNALS concurrent from multiple
threads. TSAN is the tool that would catch races in this design.

The test passes TSAN-clean: the Vyukov MPSC queue + atomic stats +
table-driven tick dispatch are race-free under concurrent load from
8 threads.

## Acceptance criteria
- [x] 8 threads × 100 items × 3 signals = 2400 total items.
- [x] All items reach sent (via null_transport + tick).
- [x] Per-signal stats match expected counts.
- [x] TSAN-clean (zero race reports).
- [x] 34/34 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
