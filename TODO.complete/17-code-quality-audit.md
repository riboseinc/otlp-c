# TODO 17 — Audit codebase for OCP, MECE, DRY, encapsulation violations

**Status:** Complete
*Closed because:* CODE-QUALITY-AUDIT.md written; findings + recommendations captured.
**Priority:** P1
**Depends on:** nothing

## Goal

Check for: (1) switch statements that should be registries, (2) module boundary violations (reaching into another module's static state), (3) code duplication across platform implementations, (4) public struct fields that should be opaque.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
