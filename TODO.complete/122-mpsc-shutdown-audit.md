# TODO 122 — MPSC + shutdown-drain audit

**Status:** Complete (v0.5.82)
**Priority:** P2 (audit outcome: core sound; contract gaps closed, protocol pinned)

## What shipped

**Audit of the lock-free MPSC queue (memory ordering, capacity
semantics) and the exporter's shutdown → drain → free path.**

**The queue core is textbook-correct Vyukov — verified by tracing
every ordering pair, not just by tests:**
- producer: plain `slot->data` write, then release-store of
  `slot->seq = h + 1` — the publish;
- consumer: acquire-load of `slot->seq`, then read `data`; then
  release-store `slot->seq = t + capacity` — the slot's return to
  producers synchronizes with the next producer's acquire at turn
  `h = t + capacity`, making the consumer's read happen-before the
  producer's overwrite. Correct pairing.
- `head`/`tail` relaxed loads/CAS are sound: both are index
  allocators; ordering flows exclusively through the per-slot
  sequence numbers. The CAS-failure path refreshes `h` (CAS writes
  the observed value into the expected operand) — no stale-slot
  retry loop.
- Full detection via `diff < 0` is exact (no spurious-full false
  positives beyond the transient), and u64 monotonic counters
  cannot wrap.
- Short-input / failure paths were checked against the v0.5.56/57
  OOM-injection coverage (init fail-path cleanup is probed).

**What was missing — the contracts around the queue:**

1. `otlp_exporter_free()` never documented the concurrency
   requirement. `shutdown()` is a cooperative stop signal, NOT a
   barrier: emits that already passed the shutdown check may still
   return OK and enqueue afterwards. An emit racing `free()` is a
   use-after-free on the queues. The exporter.h doc now states the
   required protocol explicitly (join producers after they observe
   `OTLP_ERR_SHUTDOWN`, then drain, then free).
2. `mpsc_queue_free()` now documents that it frees the slots
   array only — queued items must be drained first or they leak.

**The protocol itself had no test.** Every existing stress test
joins producers *before* calling shutdown. New
`tests/test_shutdown_stress.c` exercises the documented sequence
as written: 4 producer threads spin on `emit_move` (with
back-pressure backoff on BUFFER_FULL) until they observe
`OTLP_ERR_SHUTDOWN` on their own, while the owner thread ticks,
then shutdown → join → drain → flush → free. Asserts:
- every producer observed the stop signal;
- `stats.emitted == total accepted` (nothing accepted silently);
- the v0.5.59 stats contract `emitted == sent + dropped_err`
  holds after the drain (`dropped_full` counts rejected emits,
  which were never in `emitted` — the test corrects a common
  misreading, now encoded as an assertion with a comment).

Under ASAN + LeakSanitizer this pins use-after-free and leak
freedom for the whole protocol. (The test's first two drafts were
wrong — producers exhausted on back-pressure before shutdown, and
the accounting equation mixed rejected counts into `emitted`; the
failures were the mismeasured test, not the library.)

## Sites changed

- `include/otlp-c/exporter.h` — free() concurrency contract.
- `src/mpsc_queue.h` — free() drain contract.
- `tests/test_shutdown_stress.c` + `tests/CMakeLists.txt` — the
  protocol stress (label `unit;stress`).

## Verification

```
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout          # 38/38
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -R "shutdown-stress|concurrency"
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -E "http-timeout|url-parse"  # clean
```
