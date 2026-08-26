# TODO 16 — Build polish: CMake presets + CPack

**Status:** Complete
*Closed because:* CMakePresets.json (7 presets) + CPack config both done in v0.2 convergence pass.
**Priority:** P2
**Branch:** `v0.2-final-pass`

## Goal

Add `CMakePresets.json` for the common configurations (debug, release,
asan, tsan, vcpkg). Configure CPack to produce distributable
tarballs / installers per platform.

## Acceptance criteria

- [x] `CMakePresets.json` with presets: `debug`, `release`, `asan`, `tsan`, `vcpkg`, `windows-msvc`.
- [x] `cmake --preset release && cmake --build --preset release` works.
- [x] `CPACK_GENERATOR` set per platform: TGZ (Linux), ZIP (Windows), DragNDrop (macOS).
- [x] `cmake --build build --target package` produces a distributable.
- [x] CI's `release.yml` `build-artifacts` job uses the package target.

## Files

- `CMakePresets.json` — new.
- `CMakeLists.txt` — CPack boilerplate.

## Why

CMake presets are the modern way to give consumers + CI a single
"just build it" command. CPack gives us tarballs we can attach to
GitHub releases.
