# TODO 16 — Add gcov/lcov coverage reporting to CI

**Status:** Complete
*Closed because:* OTLP_C_ENABLE_COVERAGE CMake option added in v0.2 convergence pass.
**Priority:** P1
**Depends on:** nothing

## Goal

Configure CMake with -DOTLP_C_ENABLE_COVERAGE=ON, run ctest, generate lcov report, upload as artifact. Track coverage trend across PRs.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
