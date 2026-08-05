# TODO 11 — FreeBSD CI job fails

**Status:** WONTFIX (for v0.x)
*Closed because:* FreeBSD is continue-on-error in CI; INADDR_LOOPBACK visibility quirk is a feature-test-macro dance not worth resolving for a Tier-3 platform.
**Priority:** P1
**Depends on:** nothing

## Goal

Investigate and fix. Likely a package version mismatch or POSIX define issue specific to FreeBSD's libc.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
