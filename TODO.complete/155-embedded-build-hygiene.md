# TODO 155 — Embedded-build hygiene: FetchContent truth, no side effects

**Status:** Complete (v0.6.9)
**Priority:** P1 (documented consumer path was defective)

## The defect

v0.6.8 documented CMake FetchContent / add_subdirectory as THE
consumer path — but nothing had ever built a consumer that way
(CI only tested install + `find_package`). Built one locally and
found two side effects, both violating the library-package
principle (no writes, explicit deterministic output, consumer
controls its own tree):

1. **`set(CMAKE_INSTALL_LIBDIR "lib" CACHE STRING "" FORCE)`**
   (CMakeLists.txt install section) clobbered the PARENT
   project's cache entry. Verified: a consumer that chose
   `lib/x86_64-linux-gnu` (Debian multiarch layout) before
   `FetchContent_MakeAvailable(otlp-c)` found it silently
   rewritten to `lib` — the consumer's own libs would install to
   the wrong directory.

2. **`include(CPack)`** wrote `CPackConfig.cmake` /
   `CPackSourceConfig.cmake` into the TOP-LEVEL build dir — the
   consumer's build tree when otlp-c is embedded.

## What shipped

- `OTLP_C_IS_TOP_LEVEL` guard (`CMAKE_CURRENT_SOURCE_DIR STREQUAL
  CMAKE_SOURCE_DIR` — CMake 3.20-compatible;
  `PROJECT_IS_TOP_LEVEL` needs 3.21). The LIBDIR pin and the
  entire CPack block now run only when otlp-c IS the project.
  Embedded builds respect the parent's install layout and write
  nothing into the parent's build tree.
- New CI job **"CMake FetchContent consumer"** (ubuntu-24.04 +
  windows-2022) pins the documented path permanently: consumer
  FetchContent's the checked-out tree, builds, links, and runs a
  real emit→flush round-trip (null transport); the consumer's
  CMakeLists FATAL_ERRORs if its `CMAKE_INSTALL_LIBDIR` is
  clobbered, and the job fails if `CPackConfig.cmake` appears in
  the consumer's build dir. The exact job flow was validated
  locally before shipping.

## Verification

- Embedded: consumer LIBDIR survives (`lib/x86_64-linux-gnu`
  stays), no CPack files in consumer build dir, demo builds +
  runs + emits.
- Top-level: install still lands at `<prefix>/lib/cmake/otlp-c`
  (pin active), CPack config present, `find_package` consumer
  links and runs.
- 49/49 tests; ci.yml YAML validated.
