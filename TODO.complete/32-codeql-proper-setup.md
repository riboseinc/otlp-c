# TODO 32 — Re-enable CodeQL analysis with proper build matrix

**Status:** Complete
*Closed because:* Default CodeQL setup is enabled at the repo level; advanced workflow was removed in PR #5 to avoid conflict.
**Priority:** P1
**Depends on:** nothing

## Goal

CodeQL was dropped from checks.yml. Re-add with proper c-cpp language, correct build step, and SARIF upload to GitHub Security tab.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
