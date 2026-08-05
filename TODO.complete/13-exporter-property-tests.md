# TODO 13 — Add P-EXPORT-NEVER-CORRUPT and P-EXPORT-NO-LEAK property tests

**Status:** Ready
**Priority:** P1
**Depends on:** nothing

## Goal

P-EXPORT-NEVER-CORRUPT: any sequence of spans produces valid OTLP protobuf when POSTed. P-EXPORT-NO-LEAK: exporter create-emit-flush-shutdown-free is ASAN-clean under 10000 spans.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
