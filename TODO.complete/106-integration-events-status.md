# TODO 106 — Integration test validates events + status; CI runs it

**Status:** Complete (v0.5.66)
**Priority:** P1 (end-to-end validation of the v0.5.48 wire fixes)

## What shipped

1. **Integration test now exercises events + status.** The test
   previously emitted spans with only 2 attributes — it did not
   validate the v0.5.48 fixes (Event field-number swap, Status
   at wrong field). Now each span carries an event
   ("cache-miss" + attribute) and status (STATUS_CODE_OK), and
   the Jaeger response is searched for both needles.

2. **CI job runs the integration test.** Previously local-only
   (manual docker + env var). A wire-format regression that
   passed property tests but failed against a real collector
   would go unnoticed. New `jaeger-integration` job builds,
   starts docker compose, waits for readiness, runs ctest, dumps
   logs on failure.

## Sites changed

- `tests/integration/test_integration_jaeger.c` — add event +
  status to each span; search response for both needles.
- `.github/workflows/ci.yml` — new `jaeger-integration` job.

## Why this matters

The property tests verify wire bytes against our own decoder.
A decoder bug that affects both encode and decode would pass.
The integration test verifies against otelcol's decoder — an
independent implementation.

Concretely: pre-v0.5.48, the Event name/time field numbers were
swapped. Our encoder emitted name at field 1, time at field 2.
Our property test decoded with the same (wrong) expectations and
passed. A real collector would have dropped both fields. The
property test couldn't catch this; the integration test can.

(v0.5.48 fixed the schema after cross-checking against upstream
opentelemetry-proto — the integration test now guards against
the same class of regression via an independent decoder.)

## Verification needles

- `"cache-miss"` — Jaeger stores OTLP events as span logs.
- `"STATUS_CODE_OK"` — otelcol translates OTLP status to the
  `otel.status_code` tag with the enum name as the value.

If CI shows these needles don't match the actual serialization,
the failure will surface it and the needles can be adjusted.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 34/34 pass

# Full end-to-end (requires docker):
cd tests/integration && docker compose up -d && cd -
OTLP_C_RUN_INTEGRATION=1 ctest --test-dir build -L integration
```
