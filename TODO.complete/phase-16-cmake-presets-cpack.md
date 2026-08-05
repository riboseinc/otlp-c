# TODO 16 — Build polish: CMake presets + CPack

**Status:** Pending
**Priority:** P2
**Branch:** future (v0.2.x)

## Goal

Add `CMakePresets.json` for the common configurations (debug, release,
asan, tsan, vcpkg). Configure CPack to produce distributable
tarballs / installers per platform.

## Acceptance criteria

- [ ] `CMakePresets.json` with presets: `debug`, `release`, `asan`, `tsan`, `vcpkg`, `windows-msvc`.
- [ ] `cmake --preset release && cmake --build --preset release` works.
- [ ] `CPACK_GENERATOR` set per platform: TGZ (Linux), ZIP (Windows), DragNDrop (macOS).
- [ ] `cmake --build build --target package` produces a distributable.
- [ ] CI's `release.yml` `build-artifacts` job uses the package target.

## Files

- `CMakePresets.json` — new.
- `CMakeLists.txt` — CPack boilerplate.

## Why

CMake presets are the modern way to give consumers + CI a single
"just build it" command. CPack gives us tarballs we can attach to
GitHub releases.
