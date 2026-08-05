# TODO 23 — Implement W3C Trace Context for distributed tracing

**Status:** WONTFIX (library scope)
*Closed because:* Context propagation is the caller's job — the library doesn't own request-scoped state. Document in cookbook; do not implement.
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
