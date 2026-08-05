# TODO 20 — Windows MSVC real fix for stdatomic.h

**Status:** Pending (workaround in place)
**Priority:** P1
**Branch:** future (v0.2.x)

## Goal

Replace the `_HAS_C11_ATOMICS=1` workaround + `continue-on-error`
gating with a real fix for MSVC `<stdatomic.h>` support.

## Background

PR #4 / commit `28362f7` forced `_HAS_C11_ATOMICS=1` to bypass the
MSVC vcruntime check. CI also pins Windows to stable VS 2022 and
marks the build `continue-on-error`. This works but masks the
underlying issue: VS 18 preview's `<stdatomic.h>` is broken.

## Acceptance criteria

- [ ] Reproduce the VS 18 failure on a local MSVC install; identify the exact vcruntime version that breaks.
- [ ] Either: upgrade MSVC to a fixed version, OR fall back to compiler intrinsics (`_InterlockedCompareExchange128` etc.) on MSVC when `<stdatomic.h>` is unavailable.
- [ ] Remove `continue-on-error: ${{ contains(matrix.os, 'windows') }}` from `ci.yml`.
- [ ] Remove `_HAS_C11_ATOMICS=1` from `CMakeLists.txt` (or document why it's still needed).

## Files

- `CMakeLists.txt` — remove `_HAS_C11_ATOMICS` define (or document).
- `src/mpsc_queue.c` / `src/tracer.c` — possibly add MSVC-intrinsic fallback.
- `.github/workflows/ci.yml` — drop continue-on-error.

## Why

`continue-on-error` means Windows regressions slip through. A real
fix is needed before v1.0 promises Windows support.

## Tradeoff

MSVC intrinsic fallback doubles the implementation. Acceptable if
it lets us drop the workaround.
