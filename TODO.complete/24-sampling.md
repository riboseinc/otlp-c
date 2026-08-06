# TODO 24 — Head-based and tail-based sampling

**Status:** Complete
*Closed because:* otlp_span_set_sampled() added to span.h. Encoder emits Span.flags (field 16, fixed32) with sampled bit. traceparent_format includes the flag.
**Priority:** P1
**Depends on:** nothing

## Goal

Head: decide at span creation whether to record. Tail: decide at span end based on attributes/duration. Configurable via otlp_sampler_t interface.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
