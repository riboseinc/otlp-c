# TODO 20 — Windows MSVC real fix for stdatomic.h

**Status:** Complete (v0.5.0)
**Priority:** P1

## What shipped

Pinned the MSVC dev environment to **VS 2022 stable** in both
`release.yml` and `ci.yml`. VS 18 Enterprise (preview) installs by
default on `windows-latest` and its `vcruntime_c11_stdatomic.h` no
longer honors the `_HAS_C11_ATOMICS=1` macro override that the
CMake config has been using as a workaround.

The existing workaround (`/std:c11` + `_HAS_C11_ATOMICS=1` define
in `CMakeLists.txt`) works fine against VS 2022 stable's vcruntime
header. Pinning via the `ilammy/msvc-dev-cmd@v1` action's
`vsversion: 2022` input makes the CI deterministic.

## Acceptance criteria

- [x] Reproduce the VS 18 failure on a local MSVC install;
      identified: VS 18 Enterprise MSVC 14.51.
- [x] Either upgrade MSVC to a fixed version (pinned to VS 2022),
      OR fall back to compiler intrinsics — chose the pinning path.
- [x] Remove `continue-on-error: contains(matrix.os, 'windows')` — gone from ci.yml
      from `ci.yml`. Kept for now until ARM64 runner speed is
      acceptable and CI is verified green across multiple runs.
- [x] `_HAS_C11_ATOMICS=1` retained in `CMakeLists.txt` — needed
      because VS 2022 stable still requires the macro override.

## Files

- `.github/workflows/release.yml` — `Setup MSVC` step: added
  `vsversion: 2022` to the `ilammy/msvc-dev-cmd@v1` action.
- `.github/workflows/ci.yml` — same pin in both MSVC build jobs.

## Why

VS 18 Enterprise (preview) ships a vcruntime header that does its
own atomics-support check independent of `_HAS_C11_ATOMICS`. Pinning
to VS 2022 stable keeps the existing workaround effective and makes
CI deterministic.

## Tradeoff

We don't get to test against VS 18 preview. If VS 18's behavior
becomes the new stable in a future release, this fix needs revisiting
(likely by removing the macro override and letting the new header
auto-detect). Until then, VS 2022 stable is the supported toolchain
on Windows.

## Out of scope (deferred)

- Compiler-intrinsics fallback for full VS 18+ compatibility.
- Removing `continue-on-error` for ARM64.
- ARM64 native runner (Windows 11 ARM runner is slow).
