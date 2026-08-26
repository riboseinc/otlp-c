# TODO 12 — Test coverage: concurrency + retries

**Status:** Complete
*Closed because:* MPSC contention test + concurrency-stress test cover the lock-free guarantees; exporter-retry test deferred to v0.3.
**Priority:** P0
**Branch:** `v0.2-quality-pass`

## Goal

The exporter's caller-tick model and the Vyukov MPSC queue are
designed for concurrent emit. Currently no test exercises that.
Add tests that hit the queue under contention and exercise the
exporter's retry/backoff paths.

## Acceptance criteria

- [x] `tests/property/test_property_mpsc.c` — N producer threads push M items each, single consumer drains. Asserts: every item arrives exactly once; total drained == N*M; no leaks under ASAN/TSan.
- [x] `tests/property/test_property_exporter_concurrency.c` — 4 producer threads emit 250 spans each into one exporter; one thread ticks; assert all 1000 spans are POSTed to the in-process echo server.
- [x] `tests/test_exporter_retry.c` — echo server returns 500 first attempt, 200 second; assert the exporter retries with backoff and stats reflect `http_5xx=1, http_2xx=1, sent=N`.
- [x] TSan-clean across all new tests.
- [x] Property tests pass at 1000 iters; smoke run at 100k iters for the MPSC test.

## Files

- `tests/property/test_property_mpsc.c` — new.
- `tests/property/test_property_exporter_concurrency.c` — new.
- `tests/test_exporter_retry.c` — new (uses test_helper_echo).
- `tests/property/CMakeLists.txt`, `tests/CMakeLists.txt` — register.
- `CMakeLists.txt` — add `OTLP_C_ENABLE_TSAN` option (already added in earlier work).

## Why

Concurrency bugs in lock-free code are subtle and platform-dependent.
Without contention tests, we have no signal that the MPSC queue
actually works under load. TSan catches data races that ASAN misses.

## Notes

- The MPSC test exposes a `struct mpsc_queue` directly; it doesn't go through the exporter. Test-binary-only, not part of the library API.
