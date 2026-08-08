# TODO 61 — Configurable flush timeout + multithread example

**Status:** Complete (v0.5.21)
**Priority:** P1

## What shipped (v0.5.21)

### 1. Configurable `flush_timeout_ms`

The `flush()` and `flush_metric()` / `flush_log()` paths had a
hardcoded 30-second cap. SECURITY-ASSESSMENT.md flagged this as
LOW finding #3: "magic number. If a caller needs longer, they
must call `tick()` in a loop."

Now configurable via `otlp_exporter_opts_t.flush_timeout_ms`
(default 30000). Both flush paths use the configured value:
- `flush()`: `deadline = now_mono_ms() + e->flush_timeout_ms`
- `flush_sync()`: converted from `for (i = 0; i < 30000; i++)`
  (iteration proxy) to an explicit deadline check against
  `flush_timeout_ms`

Closes the SECURITY-ASSESSMENT finding. The assessment is updated
to mark the item as Done (v0.5.21).

### 2. Fixed: null_transport ignored `backoff_armed`

The null-transport fast path in `tick()` fired on every tick
regardless of `backoff_armed`. This made the null_transport
status callback (used to test retry/backoff) meaningless — the
callback fired `max_retries + 1` times in microseconds, dropping
the batch before any backoff logic ran. Existing retry tests
passed only because the callback returned 200 on the second call,
ending the loop before retries exhausted.

Fix: the null-transport path now checks `!e->backoff_armed`,
matching the real HTTP path. Retry/backoff is now testable
deterministically.

This bug was discovered while writing the flush timeout property
test: with `backoff_initial_ms = 10000` and `flush_timeout_ms =
200`, the test expected flush to loop for 200ms. Instead it
returned instantly because the null-transport callback exhausted
retries before the backoff timer could fire.

### 3. Multi-threaded example

`examples/multithread.c` — demonstrates the library's core
embedding pattern:

- N worker threads call `otlp_exporter_emit()` concurrently
  (lock-free MPSC queue; safe from any thread).
- One dedicated tick thread calls `otlp_exporter_tick()` in a
  loop to drain the queue and drive in-flight HTTP.
- Clean shutdown: workers join → tick thread stops → `flush()`
  drains remaining → `free()`.

Cross-platform: pthread on POSIX, CreateThread on Windows. Runs
via null_transport so it works without a local collector. The
example is registered in `examples/CMakeLists.txt` and links
pthread on Linux.

### 4. Flush timeout property test

`tests/property/test_property_flush_timeout.c` — verifies a
custom `flush_timeout_ms` (200ms) is respected:

- Sets `flush_timeout_ms = 200`, `max_retries = 1000`,
  `backoff_initial_ms = 10000`.
- Null_transport with a status callback returning 500 → arms
  backoff → tick can't progress → flush loops.
- Measures wall-clock elapsed; asserts 150ms ≤ elapsed < 5000ms
  (i.e., near the configured 200ms, NOT the 30s default).

POSIX-only (uses `clock_gettime`). Gated accordingly.

## Why this matters

**The flush timeout was a real usability bug.** Production callers
embedding `otlp-c` into latency-sensitive contexts (kernel modules,
firmware) need bounded shutdown. The hardcoded 30s cap meant a
dead collector would block shutdown for half a minute. Configurable
timeout lets callers pick the right tradeoff for their context.

**The null_transport backoff bug was a real test-quality bug.**
Without the fix, retry/backoff tests via null_transport were
testing nothing — the callback exhausted retries before backoff
could fire. This means any future regression in the retry/backoff
path would not have been caught by null_transport-based tests.

**The multithreaded example demonstrates the core selling point.**
The library's #1 differentiator is thread-safe emit + caller-tick
with no library threads. Until v0.5.21, this pattern existed only
in test code (`test_concurrency_stress.c`), not in examples. Real
users evaluating the library had no runnable demo of the pattern.

## Acceptance criteria
- [x] `flush_timeout_ms` field on `otlp_exporter_opts_t`.
- [x] Default 30000; configurable via opts.
- [x] Both `flush()` and `flush_sync()` respect the configured value.
- [x] Null-transport path respects `backoff_armed`.
- [x] Multi-threaded example builds + runs on POSIX and Windows.
- [x] Property test verifies the timeout fires near the configured value.
- [x] 29/29 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
- [x] SECURITY-ASSESSMENT.md finding #3 marked resolved.
