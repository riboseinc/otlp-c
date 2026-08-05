# TODO 10 — CMake find_package consumer test fails on some platforms

**Status:** Ready
**Priority:** P1
**Depends on:** nothing

## Goal

the config template must use CMakeFindDependencyMacro instead of CMakeFindDependencyFramework. Commit 53040d2 partially addresses this; verify the fix works on Windows too.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
