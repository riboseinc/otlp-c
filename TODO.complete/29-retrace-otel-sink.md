# TODO 29 — Integrate otlp-c into retrace as the OTel sink

**Status:** Ready
**Priority:** P1
**Depends on:** nothing

## Goal

The original use case. retrace's src/sinks/otel/ links against otlp-c via find_package. Span bridge: retrace log entry to otlp_span_t.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
