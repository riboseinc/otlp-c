# TODO 170 — Descriptor audit of all 32 tables; PartialSuccess fix

**Status:** Complete (v1.0.2)
**Priority:** P0 (second released wire bug)

## The audit tool

tests/golden/audit_tables.py: parses every OTLP_*_FIELDS table
from src/otlp_schema.h and verifies each field's number and
computed wire type against the INSTALLED opentelemetry-proto
descriptors (name map for our abbreviations; packed-repeated
scalars correctly expected as LEN). Exit code gates. Manual run,
like generate.py — documented as the companion to any schema
change.

## The bug it found (30 seconds in)

ExportXServiceResponse.partial_success is field **1** in the
proto; our decoder table said **5** since v0.5.96. Effect:
PartialSuccess payloads from REAL collectors were silently
ignored — the WARN diagnostic, the PARTIAL_SUCCESS event, and the
rejected_* stats never fired for genuine server-side data loss.
Hidden for 100+ releases because every fixture (echo responses
in test_exporter_partial_success, hand-built bodies in
test_unit_protobuf) encoded field 5 to match the same wrong
table — self-referential, the v1.0.1 failure class exactly.

## Fixes

- Table: 5 → 1 (with a comment naming the bug and its finder).
- Fixtures: echo response builder 0x2a → 0x0a; protobuf unit
  build_response field 5 → 1; raw literals updated; comments
  corrected.
- EPS table field name "rejected" → "rejected_spans" (trace
  variant; names are documentation-only in decode).
- **Reference-validated decode**: generate.py now emits a
  reference-encoded ExportTraceServiceResponse
  (rejected_spans=3, error_message="queue full") — GOLDEN_
  TRACES_RESPONSE — and unit-golden decodes it with the
  PRODUCTION decoder. Decode is no longer only tested against
  our own encodings.

## Verification

audit_tables.py: 32/32 OK. 52/52 via every preset; Doxygen zero
warnings.
