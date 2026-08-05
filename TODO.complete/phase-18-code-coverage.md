# TODO 18 — Code coverage

**Status:** Pending
**Priority:** P2
**Branch:** future (v0.3+)

## Goal

Wire up gcov / llvm-cov coverage. Generate HTML reports. Upload to
Codecov or similar. Set a coverage floor (e.g. 80%) that fails CI.

## Acceptance criteria

- [ ] `OTLP_C_ENABLE_COVERAGE` CMake option (gcc + clang).
- [ ] CI job runs tests with coverage, uploads to codecov.io.
- [ ] README badge with current coverage %.
- [ ] Coverage floor enforced (CI fails if % drops below threshold).

## Files

- `CMakeLists.txt` — coverage option + flags.
- `.github/workflows/ci.yml` — coverage job.
- `README.md` — badge.

## Why

Coverage isn't a quality gate by itself, but it shows where the
gaps are. The property tests already cover the wire format well;
coverage shows whether the exporter / HTTP client / URL parser are
exercised at every branch.
