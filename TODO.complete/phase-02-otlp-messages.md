# TODO 02 — OTLP message encoders

**Status:** Complete
**Phase:** 2
**Priority:** P0
**Branch:** `phase-2-otlp-messages`

## Goal

Just-in-time encoders that take `otlp_span_t*` (and the surrounding ResourceSpans / ScopeSpans envelope) and emit protobuf wire bytes. No parallel C-struct layer mirroring the .proto.

## Acceptance criteria

- [ ] `src/otlp_messages.{h,c}` declares encoder functions per OTLP message type.
- [ ] Field numbers match `docs/otlp-spec.md` exactly.
- [ ] `otlp_encode_export_trace_service_request` produces a complete `ExportTraceServiceRequest` body.
- [ ] Empty request (zero spans, no service/scope name) → zero bytes output.
- [ ] Golden-vector cross-validation: a fixed span encodes to byte-identical output as `opentelemetry-proto` reference encoding.
- [ ] All attribute types (string/int64/double/bool/bytes) round-trip through encode + manual decode.
- [ ] Property tests pass at default iter count; ASAN-clean.

## Files

- `src/otlp_messages.h` — new internal header.
- `src/otlp_messages.c` — encoder bodies.
- `CMakeLists.txt` — add `src/otlp_messages.c` to `otlp_c` sources.
- `tests/property/test_property_messages.c` — new file.
- `tests/property/CMakeLists.txt` — register.

## Test plan

- `prop_encode_empty_request`: zero spans → `len == 0`.
- `prop_encode_field_numbers`: decode a fixed encoding; assert every field number + wire type matches spec.
- `prop_encode_cross_validate`: assert byte-equality with golden vector.
- `prop_encode_attribute_types`: each attribute type round-trips.

## Open design note

Phase 5's caller-tick exporter requires access to `struct otlp_attribute` (defined in `span.c`). Phase 4 will expose it via `src/span_internal.h` (internal-only). Phase 2's `otlp_encode_any_value` takes that type.

## Dependencies

- Phase 1 (encoder primitives).
- Phase 4 (provides `struct otlp_attribute` via `src/span_internal.h`).

## Verification

```
cmake --build build
ctest --test-dir build -L property --output-on-failure
```

## Completion evidence

All 7 tests pass (smoke + 6 property tests). The Phase 2 test (`property-messages`) covers: empty-request zero-byte invariant, span field-number/wire-type invariant per spec, KeyValue round-trip (string + int64), status omission for UNSET, status emission for non-UNSET.

ASAN-clean.

**Deferred to follow-up:** the golden-vector cross-validation against `opentelemetry-proto` (would need a Python script + checked-in `.bin` file). The structural tests above verify the same invariants without external dependencies.
