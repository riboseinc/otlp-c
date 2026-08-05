# TODO 18 — Verify every public API function validates arguments

**Status:** Ready
**Priority:** P1
**Depends on:** nothing

## Goal

Every function taking a pointer should check for NULL. Every function taking a string should check for empty. Every function taking a count should check for zero. Document the validation in each function's docstring.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
