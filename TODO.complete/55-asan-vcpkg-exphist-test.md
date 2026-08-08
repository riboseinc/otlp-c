# TODO 55 — ASAN CI + vcpkg port sync + ExpHistogram setter test

**Status:** Complete (v0.5.14)
**Priority:** P2

## What shipped (v0.5.14)

### Added — AddressSanitizer CI job

New `asan` job in `.github/workflows/ci.yml`: builds with
`-DOTLP_C_ENABLE_ASAN=ON` on Ubuntu 24.04 and runs the full test
suite with `detect_leaks=1:abort_on_error=1`. Catches use-after-free,
buffer overflow, heap corruption that property tests alone might miss.

### Fixed — vcpkg overlay port version sync

`ports/otlp-c/vcpkg.json` was stuck at 0.3.0; `portfile.cmake`
referenced `REF v0.3.0`. Updated to 0.5.14.

### Added — ExpHistogram setter property test

`prop_flush_exp_histogram_with_buckets` in `test_property_flush.c`:
creates an ExpHistogram, sets scale + positive bucket counts via
`otlp_metric_set_exp_histogram()`, flushes via null_transport,
verifies OK.

## Acceptance criteria
- [x] ASAN CI job added and passing.
- [x] vcpkg overlay port version matches release.
- [x] ExpHistogram setter tested.
- [x] All 27 ctest tests pass (28 internal properties).
