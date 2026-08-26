# TODO 10 — CMake find_package consumer test fails on some platforms

**Status:** Complete
*Closed because:* otlp-c-config.cmake.in uses find_dependency(Threads); consumer test passes.
**Priority:** P1
**Depends on:** nothing

## Goal

the config template must use CMakeFindDependencyMacro instead of CMakeFindDependencyFramework. Commit 53040d2 partially addresses this; verify the fix works on Windows too.

## Tasks

### P0
- [x] Implement

### P1
- [x] Test

## Acceptance criteria
- [x] CI green on all platforms
- [x] No regression in existing tests
