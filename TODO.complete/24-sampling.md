# TODO 24 — Head-based and tail-based sampling

**Status:** WONTFIX (library scope)
*Closed because:* Sampling is a caller decision (head-based) or a collector decision (tail-based). The library emits what it's told.
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
