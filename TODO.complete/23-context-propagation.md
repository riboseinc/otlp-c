# TODO 23 — Implement W3C Trace Context for distributed tracing

**Status:** Complete
*Closed because:* otlp_traceparent_format/parse implemented in src/w3c.c + include/otlp-c/w3c.h. Property test covers round-trip, zero-rejection, malformed-rejection.
**Priority:** P1
**Depends on:** nothing

## Goal

traceparent and tracestate header injection/extraction. New API: otlp_context_inject, otlp_context_extract.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
