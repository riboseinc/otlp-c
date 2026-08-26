# TODO 14 — Multi-threaded stress tests for MPSC queue and tracer

**Status:** Complete
*Closed because:* test_concurrency_stress.c merged (8 threads × 200 spans).
**Priority:** P1
**Depends on:** nothing

## Goal

Spawn N threads, each emitting M spans concurrently. Verify: (1) no crash, (2) all spans accounted for, (3) no duplicates in trace/span IDs, (4) ASAN-clean.

## Tasks

### P0
- [x] Implement

### P1
- [x] Test

## Acceptance criteria
- [x] CI green on all platforms
- [x] No regression in existing tests
