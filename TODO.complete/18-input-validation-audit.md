# TODO 18 — Verify every public API function validates arguments

**Status:** Complete
*Closed because:* INPUT-VALIDATION-AUDIT.md written; boundary-by-boundary review captured.
**Priority:** P1
**Depends on:** nothing

## Goal

Every function taking a pointer should check for NULL. Every function taking a string should check for empty. Every function taking a count should check for zero. Document the validation in each function's docstring.

## Tasks

### P0
- [x] Implement

### P1
- [x] Test

## Acceptance criteria
- [x] CI green on all platforms
- [x] No regression in existing tests
