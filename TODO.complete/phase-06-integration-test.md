# TODO 06 — Integration test against otelcol + Jaeger

**Status:** Complete
**Phase:** 6
**Priority:** P1
**Branch:** `phase-6-integration`

## Goal

End-to-end test: real `otelcol` container + real Jaeger container, real spans emitted via the caller-tick exporter, real Jaeger query confirms visibility.

## Acceptance criteria

- [x] `tests/integration/docker-compose.yml` brings up `otelcol` + `jaeger`.
- [x] `tests/integration/otelcol-config.yaml` configures otelcol receiver (otlp http :4318) and exporter (jaeger).
- [x] `tests/integration/test_integration_jaeger.c` emits 100 spans with a unique `test_run_id`, drives `tick()` until drained, then queries Jaeger API.
- [x] Test asserts the service appears in `/api/services` and ≥100 traces appear in `/api/traces?...&tags={"test_run_id":"..."}`.
- [x] `tests/CMakeLists.txt` uncommented for `add_subdirectory(integration)`.
- [x] `.github/workflows/build.yml` extended with an `integration` job (Linux + macOS).
- [x] Test gated by `OTLP_C_RUN_INTEGRATION=1` so missing Docker doesn't fail local dev.
- [x] `docs/integration-test.md` explains how to run locally + shows the sidecar topology.

## Files

- `tests/integration/docker-compose.yml` — new.
- `tests/integration/otelcol-config.yaml` — new.
- `tests/integration/test_integration_jaeger.c` — new.
- `tests/integration/CMakeLists.txt` — new.
- `tests/CMakeLists.txt` — uncomment `add_subdirectory(integration)`.
- `.github/workflows/build.yml` — integration job.
- `docs/integration-test.md` — new.

## Test plan

- The integration binary is the test. Label: `integration` (no `unit`).
- Polls Jaeger for up to 10s with exponential backoff.

## Dependencies

- Phase 5.

## Verification

```
cd tests/integration && docker compose up -d && cd -
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build
OTLP_C_RUN_INTEGRATION=1 ctest --test-dir build -L integration --output-on-failure
```
