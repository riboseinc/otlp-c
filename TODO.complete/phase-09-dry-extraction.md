# TODO 09 — DRY: extract shared helpers

**Status:** In Progress
**Priority:** P1
**Branch:** `v0.2-quality-pass`

## Goal

Kill the duplicate `dup_str` (src/) and `decode_varint` (tests/) helpers
that exist in 3+ files each. Extract to a single shared location.

## Acceptance criteria

- [ ] `src/internal_utils.h` declares `otlp_dup_str()` and `otlp_dup_bytes()`.
- [ ] `src/internal_util.c` implements them.
- [ ] span.c, tracer.c, exporter.c use the shared helpers; their static `dup_str` removed.
- [ ] `tests/property/decoder.h` declares `decode_varint()`, `decode_tag()`, `skip_value()`.
- [ ] test_property_varint.c, test_property_encoder.c, test_property_messages.c use the shared decoder.
- [ ] All existing tests still pass.

## Files

- `src/internal_util.{h,c}` — new.
- `src/span.c`, `src/tracer.c`, `src/exporter.c` — drop static dup_str.
- `tests/property/decoder.h` — new.
- 3 property test files — drop static decode_varint.

## Why

Three copies of the same function drift over time. When a bug is
found in one copy, the others don't always get fixed. A single
shared helper is the only way to keep them aligned.
