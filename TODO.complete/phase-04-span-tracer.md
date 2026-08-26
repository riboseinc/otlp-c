# TODO 04 — Span builder + tracer

**Status:** Complete
**Phase:** 4
**Priority:** P0
**Branch:** `phase-4-span-tracer`

## Goal

Real `struct otlp_span` definition with all 14 setters, fixed-cap attribute array, and lock-free atomic PRNG for trace/span ID generation in `struct otlp_tracer`.

## Acceptance criteria

- [x] `src/span.c` defines `struct otlp_span` with inline trace/span/parent IDs, name, times, kind, fixed-cap attribute array, status. No locks (span is single-threaded by API contract).
- [x] All 14 setters work; correct error codes on NULL / overflow.
- [x] `src/span_internal.h` exposes `struct otlp_attribute` + accessors for use by Phase 2's encoder.
- [x] `src/tracer.c` defines `struct otlp_tracer` with xorshift128+ state, lock-free via C11 `<stdatomic.h>` CAS.
- [x] Trace IDs: 16 random bytes, version nibble = 1 (W3C Trace Context).
- [x] Span IDs: 8 random bytes, reject all-zero (regenerate).
- [x] `start_child_span` correctly links parent: `has_parent = true`, `parent_span_id` copied.
- [x] `mark_start` / `mark_end` use monotonic clock, convert to wall-clock for storage.
- [x] Property tests: attribute round-trip, ID lengths, ID uniqueness, parent linking, monotonic times.
- [x] ASAN-clean across all property tests.

## Files

- `src/span.c` — replace stub.
- `src/span_internal.h` — new (internal attribute accessor).
- `src/tracer.c` — replace stub.
- `tests/property/test_property_span.c` — new.
- `tests/property/test_property_attribute_roundtrip.c` — new.
- `tests/property/CMakeLists.txt` — register.

## Architectural decisions

- `OTLP_SPAN_MAX_ATTRIBUTES = 128` compile-time cap. Returns `OTLP_ERR_OVERFLOW` past this. Dynamic array is post-1.0.
- Trace ID version nibble: bit pattern `0x0000000000000000xxxxxxxxxxxxxxx` with the version byte set to 1 (W3C Trace Context level 1).
- Tracer PRNG seeded from `otlp_platform_now_mono_nano ^ thread_id ^ pid`.
- Per-tracer PRNG state, atomic CAS on the 128-bit state (or 64-bit segment + lock-free retry on collision).

## Dependencies

- Phases 1, 2 (encoder used by integration tests; not by span/tracer itself).
- `src/platform.h` for clock + atomics.

## Verification

```
cmake --build build
ctest --test-dir build -L property --output-on-failure
OTLP_C_PROPERTY_ITERS=100000 ctest --test-dir build -R span
```
