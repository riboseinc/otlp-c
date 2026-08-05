# TODO 01 — Protobuf wire encoder

**Status:** Complete
**Phase:** 1
**Priority:** P0
**Branch:** `phase-1-protobuf-encoder`

## Goal

Hand-rolled protobuf wire encoder for the four wire types OTLP uses (varint, fixed64, fixed32, length-delimited). No third-party protobuf library. Foundation for Phase 2 (message encoders).

## Acceptance criteria

- [x] All 13 functions declared in `src/protobuf_encode.h` implemented.
- [x] `otlp_pb_buf_init` / `_free` / `_reset` manage memory correctly; ASAN-clean.
- [x] Varint encoding: 7 bits/byte, MSB continuation, 1–10 bytes for `uint64_t`.
- [x] Fixed64/fixed32: little-endian, no platform dependency (use shifts, not unions).
- [x] `otlp_pb_tag` emits `(field_num << 3) | wire_type` as a varint.
- [x] Typed field helpers skip default values per protobuf3 semantics (varint=0, fixed=0, empty string, empty bytes, empty sub-message).
- [x] Buffer growth is amortized O(1) (doubling strategy), with size_t overflow guard.
- [x] Property tests: varint round-trip, varint byte-size invariant, varint extremes (0 and UINT64_MAX), typed field round-trip, buf growth.
- [x] Property tests pass at default iter count; ASAN-clean.

## Files

- `src/protobuf_encode.c` — replace empty stub.
- `tests/property/test_property_varint.c` — new file.
- `tests/property/test_property_encoder.c` — new file.
- `tests/property/CMakeLists.txt` — register the two new tests.

## Test plan

- `prop_varint_roundtrip`: PRNG emits `uint64_t`; encode; manually decode; assert equal.
- `prop_varint_size`: encoded length = `max(1, ceil(log2(n+1)/7))`.
- `prop_varint_extremes`: encode 0 and `UINT64_MAX`, assert specific byte patterns.
- `prop_field_roundtrip`: encode each typed field helper; decode; assert field number + wire type + value preserved.
- `prop_buf_growth`: emit many small fields; assert `cap` grows monotonically and never overflows.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build -L property --output-on-failure
```

## Completion evidence

Build: `cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON && cmake --build build` — clean.

Tests (default iter count):
```
4/4 Test #4: property-encoder .................   Passed    0.23 sec
100% tests passed out of 4
```

High-iteration run (`OTLP_C_PROPERTY_ITERS=100000`):
```
property-varint .... Passed   0.07 sec
property-encoder ... Passed   4.42 sec
100% tests passed
```

ASAN run (`-DOTLP_C_ENABLE_ASAN=ON`): all 4 tests pass, no leaks reported.
