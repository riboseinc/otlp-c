# TODO 11 — FreeBSD CI job fails

**Status:** Complete (v1.1.3; WONTFIX stood only for v0.x)
*Closed because:* the quirk WAS worth resolving — tests/test_portable.h
(one deterministic code path per platform) + nanosleep + one Threads link
fixed every masked failure; FreeBSD 14.2 has gated the full suite since
v1.1.3 (PR #170, TODO 178 records the continue-on-error mask lesson).
**Priority:** P1
**Depends on:** nothing

## Goal

Investigate and fix. Likely a package version mismatch or POSIX define issue specific to FreeBSD's libc.

## Tasks

### P0
- [x] Implement

### P1
- [x] Test

## Acceptance criteria
- [x] CI green on all platforms
- [x] No regression in existing tests
