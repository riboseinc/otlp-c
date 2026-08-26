# TODO 32 — Re-enable CodeQL analysis with proper build matrix

**Status:** Complete
*Closed because:* Default CodeQL setup is enabled at the repo level; advanced workflow was removed in PR #5 to avoid conflict.
**Priority:** P1
**Depends on:** nothing

## Goal

CodeQL was dropped from checks.yml. Re-add with proper c-cpp language, correct build step, and SARIF upload to GitHub Security tab.

## Tasks

### P0
- [x] Implement

### P1
- [x] Test

## Acceptance criteria
- [x] CI green on all platforms
- [x] No regression in existing tests
