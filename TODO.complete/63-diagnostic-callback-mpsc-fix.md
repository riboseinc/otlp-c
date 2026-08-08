# TODO 63 — Diagnostic callback + MPSC queue capacity fix

**Status:** Complete (v0.5.23)
**Priority:** P0 (data-loss bug fix) + P1 (observability feature)

## What shipped (v0.5.23)

Two changes bundled because the diagnostic feature uncovered the
bug:

### 1. CRITICAL FIX: MPSC queue never enforced capacity

The bounded MPSC queue's sequence-number formulas deviated from
the canonical Vyukov scheme in a way that broke the full check:

| | Before (buggy) | After (canonical) |
|---|---|---|
| init | `seq[i] = i + 1` | `seq[i] = i` |
| push diff | `seq - (h + 1)` | `seq - h` |
| push release | `h + capacity + 1` | `h + 1` |
| pop diff | `seq - (t + capacity + 1)` | `seq - (t + 1)` |
| pop release | `t + capacity + 1` | `t + capacity` |

**The bug:** after a push at turn h released `seq = h + capacity + 1`,
the producer's wrap-around push at turn `h + capacity` saw
`diff = (h + capacity + 1) - (h + capacity + 1) = 0` → "ready",
overwriting the unconsumed slot. The `diff < 0` full check never
fired. The queue claimed to be bounded but was unbounded.

**Impact:** silent data loss in production. Whenever the consumer
(tick) couldn't keep up with producers (emit), spans were silently
overwritten. No error returned, no log emitted, no stat
incremented. The caller had no way to know.

**Why it wasn't caught sooner:**
- The concurrency stress test runs tick() concurrently with
  emitters, so the consumer always kept up.
- No test emitted into a full queue without ticking.
- No diagnostic surfaced "I dropped a span" — the code path was
  dead (push never returned BUFFER_FULL).

**The diagnostic test caught it.** The first version of
`prop_diag_fires_on_queue_full` emitted 20 spans into a
capacity-4 queue and expected WARN logs. All 20 emits returned
OK; zero WARNs fired. Investigation traced to the MPSC formulas.

**The fix** restores the canonical Vyukov scheme. Now:
- After push at h: `seq = h + 1` (low number).
- Wrap-around push at `h + capacity`: `diff = (h + 1) - (h + capacity) < 0` → FULL.
- After pop at t: `seq = t + capacity` (matches the next producer turn).

Verified: 20 emits into capacity-4 queue → 4 succeed, 16 return
`OTLP_ERR_BUFFER_FULL`, 16 WARN logs fire. Correct.

### 2. Diagnostic callback (`otlp_exporter_set_logger`)

```c
typedef enum {
    OTLP_LOG_DEBUG,  /* routine (batch sent) */
    OTLP_LOG_INFO,   /* expected (retry armed) */
    OTLP_LOG_WARN,   /* degraded (queue full, transient retry) */
    OTLP_LOG_ERROR,  /* unexpected (max retries, permanent 4xx) */
} otlp_log_level_t;

typedef void (*otlp_log_fn)(void *ctx, otlp_log_level_t level,
                             const char *message);

void otlp_exporter_set_logger(otlp_exporter_t *exp,
                               otlp_log_fn fn, void *ctx);
```

Wired at 7 events:

| Event | Level | Message format |
|---|---|---|
| emit/emit_move queue full | WARN | "span dropped: queue full (size=%zu)" |
| network error → retry | WARN | "network error; retry %u/%u in %ums" |
| network error → max retries | ERROR | "network error: %llu spans dropped (max retries %u)" |
| 5xx → retry | WARN | "HTTP %d; retry %u/%u in %ums" |
| 5xx → max retries | ERROR | "HTTP %d: %llu spans dropped (max retries %u)" |
| 4xx permanent | ERROR | "HTTP %d: %llu spans dropped (permanent)" |
| 2xx success | DEBUG | "batch sent: %llu spans" |

**Design:**
- **Per-exporter, not global.** Different exporters can have
  different loggers. OCP: no existing API changed.
- **Optional.** Default: no callback. Every log site compiles to
  a NULL-pointer check — zero observable overhead in hot paths.
- **Thread-safe by contract.** The callback may fire from any
  thread (emit from any caller, tick from the tick thread).
  Documented; the implementation MUST be thread-safe.
- **`message` is transient.** Valid only for the duration of the
  call. Callers copy if they need it longer.

**Implementation:** a static `otlp_log(e, level, fmt, ...)` helper
in exporter.c does the NULL-check, formats via `vsnprintf` into a
stack buffer, and dispatches. DRY: one formatting path, 7 call
sites.

### 3. Diagnostic property tests

`tests/property/test_property_diagnostics.c` (4 properties):
- `prop_diag_fires_on_queue_full` — **this is the test that
  caught the MPSC bug.** Emits 20 spans into capacity-4 queue,
  expects WARN with "queue full". Before the MPSC fix: 0 WARNs.
  After: 16 WARNs.
- `prop_diag_fires_on_4xx_permanent` — null_transport returning
  404 fires ERROR with "permanent".
- `prop_diag_fires_on_success` — successful send fires DEBUG
  with "batch sent".
- `prop_diag_disabled_by_default` — no callback = no crash, no
  hang. Exercises the NULL-check zero-overhead path.

## Why this matters

**The MPSC bug was the most serious defect in the library.** A
bounded queue that doesn't enforce bounds defeats the library's
core value prop: "lock-free, non-blocking, safe to embed." In
production, a slow collector or a bursty workload would silently
lose spans — the caller sees `emit() == OK` and assumes delivery,
but the span was overwritten before it reached the network.

**The diagnostic callback prevents the next such bug from hiding.**
Before: the only signal was `dropped_full` counter, which
incremented only on `OTLP_ERR_BUFFER_FULL` returns — which never
happened because the queue never reported full. After: the WARN
log fires unconditionally on every drop attempt, making the
failure visible in real time.

## Verification

```
ctest --test-dir build          # 31/31 pass (was 30 + new diagnostics)
ctest --test-dir build-tsan     # 31/31 pass, zero race reports
ctest --test-dir build-asan     # 31/31 pass (ASAN + UBSAN)
```

Zero compiler warnings across all three configurations. The
diagnostic test that caught the bug now passes; the MPSC queue
correctly returns `OTLP_ERR_BUFFER_FULL` on overflow.

## Acceptance criteria
- [x] MPSC init/push/pop formulas match canonical Vyukov scheme.
- [x] Push returns `OTLP_ERR_BUFFER_FULL` on actual overflow.
- [x] `otlp_log_level_t` + `otlp_log_fn` types in exporter.h.
- [x] `otlp_exporter_set_logger` function.
- [x] 7 diagnostic call sites in exporter.c.
- [x] Property tests: queue-full, 4xx permanent, success, disabled.
- [x] 31/31 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
