# TODO 05 — Caller-tick exporter with lock-free MPSC

**Status:** Complete
**Phase:** 5
**Priority:** P0
**Branch:** `phase-5-exporter`

## Goal

Batching exporter driven by caller-invoked `otlp_exporter_tick()`. Lock-free MPSC queue (Vyukov). Non-blocking HTTP state machine. No library threads, no mutexes, no condvars. Exponential backoff with full jitter on transient failures.

## Acceptance criteria

- [x] `src/mpsc_queue.h` implements bounded Vyukov MPSC queue on C11 atomics.
- [x] `src/exporter.c` replaces stub with real implementation.
- [x] `src/exporter_otel.{h,c}` provides `otlp_exporter_otel_build_request` (encode once, drive HTTP via `_step`).
- [x] Public API extended: `otlp_exporter_tick(exp, max_wait_ms)` and `otlp_exporter_poll_fds(...)`.
- [x] Public API docstring updated: remove "background thread" claim.
- [x] 1000 spans emitted + ticked → exactly 2 POSTs received (or 1 if flushed within `batch_ms`).
- [x] Failed POST (429 / 5xx / network) triggers backoff with full jitter; eventual success increments `sent`.
- [x] Permanent failure (4xx non-429) drops batch, increments `dropped_err`.
- [x] Queue overflow returns `OTLP_ERR_BUFFER_FULL`, increments `dropped_full`.
- [x] `shutdown()` sets atomic flag; subsequent `emit()` returns `OTLP_ERR_SHUTDOWN`.
- [x] `flush()` blocks calling thread in `tick()` loop until drained or budget exhausted.
- [x] `free()` releases all memory; ASAN-clean.
- [x] TSan-clean across concurrent emit + tick.

## Files

- `src/mpsc_queue.h` — new (header-only, atomics).
- `src/exporter_otel.h`, `src/exporter_otel.c` — new.
- `src/exporter.c` — replace stub.
- `include/otlp-c/exporter.h` — extend (tick, poll_fds, updated docstrings).
- `tests/property/test_property_exporter_batching.c` — new.
- `tests/property/test_property_exporter_shutdown.c` — new.
- `tests/test_exporter_echo.c` — new (unit).
- `tests/CMakeLists.txt` — register.
- `CMakeLists.txt` — add `OTLP_C_ENABLE_TSAN` option; remove `Threads::Threads` link (no longer needed since no library threads).

## Architectural decisions

- Vyukov MPSC: bounded ring, `head` atomic (producer-CAS), `tail` atomic (consumer-store). Capacity 4096 spans default.
- Cache-line padding around `head` and `tail` to avoid false sharing.
- Per-exporter state: in-flight HTTP request ≤ 1 for v0.1.0.
- Backoff: `min(max, initial * 2^attempt)` with full jitter `random[0, delay)`.
- `emit()` deep-copies the span into the queue (caller may free immediately). Documented in docstring.

## Embedding patterns supported

- No event loop: `tick(exp, 0)` after each emit.
- Periodic tick: `tick(exp, 0)` per frame / iteration.
- Caller-owned worker thread: caller spawns thread, loops `tick(exp, 100)`.
- Caller event loop (libuv/epoll/kqueue/IOCP): `poll_fds()` + tick on event.
- Language VM binding: wrap `tick()` as a timer callback.

## Dependencies

- Phases 1-4.

## Verification

```
cmake --build build
ctest --test-dir build --output-on-failure
cmake -B build-tsan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan -L property
```
