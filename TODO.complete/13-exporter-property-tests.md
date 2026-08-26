# TODO 13 — Add P-EXPORT-NEVER-CORRUPT and P-EXPORT-NO-LEAK property tests

**Status:** Complete
*Closed because:* tests/property/test_property_exporter.c implements 2 property tests (random batch_size/n_spans flush, empty emit) × 50 iterations. Uses in-process echo server; POSIX-gated.
**Priority:** P1
**Depends on:** nothing

## Goal

P-EXPORT-NEVER-CORRUPT: any sequence of spans produces valid OTLP protobuf when POSTed. P-EXPORT-NO-LEAK: exporter create-emit-flush-shutdown-free is ASAN-clean under 10000 spans.

## Tasks

### P0
- [x] Implement

### P1
- [x] Test

## Acceptance criteria
- [x] CI green on all platforms
- [x] No regression in existing tests
