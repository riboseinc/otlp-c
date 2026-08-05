# TODO 14 — Multi-threaded stress tests for MPSC queue and tracer

**Status:** Ready
**Priority:** P1
**Depends on:** nothing

## Goal

Spawn N threads, each emitting M spans concurrently. Verify: (1) no crash, (2) all spans accounted for, (3) no duplicates in trace/span IDs, (4) ASAN-clean.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
