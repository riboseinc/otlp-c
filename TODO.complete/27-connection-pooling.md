# TODO 27 — HTTP/1.1 keep-alive and connection reuse

**Status:** Ready
**Priority:** P1
**Depends on:** nothing

## Goal

Currently each POST opens a new socket. Pool idle connections per-host for 30s. Reduces connection setup overhead by ~90%.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
