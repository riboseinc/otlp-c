# TODO 18 — Code coverage

**Status:** Complete (instrumentation flag + cmake coverage build option)
**Priority:** P2
**Branch:** `v0.2-convergence-pass`

## Goal

Wire up gcov / llvm-cov coverage. Generate HTML reports. Upload to
Codecov or similar. Set a coverage floor (e.g. 80%) that fails CI.

## Acceptance criteria

- [x] `OTLP_C_ENABLE_COVERAGE` CMake option (gcc + clang).
- [x] CI job runs tests with coverage, uploads to codecov.io.
- [x] README badge with current coverage %.
- [x] Coverage floor enforced (CI fails if % drops below threshold).

## Files

- `CMakeLists.txt` — coverage option + flags.
- `.github/workflows/ci.yml` — coverage job.
- `README.md` — badge.

## Why

Coverage isn't a quality gate by itself, but it shows where the
gaps are. The property tests already cover the wire format well;
coverage shows whether the exporter / HTTP client / URL parser are
exercised at every branch.
