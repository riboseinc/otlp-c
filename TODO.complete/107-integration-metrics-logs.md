# TODO 107 — Integration test covers all three signals

**Status:** Complete (v0.5.67)
**Priority:** P1 (end-to-end validation of the metrics/logs encoders)

## What shipped

1. **Integration test emits a metric + a log record.** A counter
   with an attribute via `flush_metric`; a log record with
   severity/body/attribute via `flush_log`. Both assert a 2xx
   from otelcol — the collector's real protobuf parser accepted
   the `ExportMetricsServiceRequest` / `ExportLogsServiceRequest`.

2. **otelcol config gains metrics + logs pipelines** with the
   `debug` exporter (prints received data to stderr). Previously
   only a traces pipeline existed — metrics/logs POSTs would
   have been rejected with "no pipeline configured".

3. **Two new CI steps grep otelcol logs** for the metric name and
   log body, confirming they arrived intact after the batch
   processor's ~5s hold (retry loop handles the delay).

## Sites changed

- `tests/integration/otelcol-config.yaml` — add `debug` exporter
  + metrics/logs pipelines.
- `tests/integration/test_integration_jaeger.c` — emit metric +
  log after the spans; assert flush_metric/flush_log return OK.
- `.github/workflows/ci.yml` — two verification steps that grep
  `docker compose logs otelcol`.

## Why this matters

The metrics/logs encoder bugs found in v0.5.48-v0.5.49 were
invisible to property tests for 49 releases — our decoder shared
the same wrong expectations. Cross-checking against upstream
opentelemetry-proto found them; otelcol's independent parser now
guards against their return on every PR.

## All three signals validated end-to-end

| Signal | Test path | CI verification |
|---|---|---|
| Traces | emit → flush → Jaeger query | run_id + event + status needles |
| Metrics | flush_metric → 2xx | otelcol debug-exporter grep |
| Logs | flush_log → 2xx | otelcol debug-exporter grep |

## Verification

```
# Local (34/34 pass without docker):
cmake --build build
ctest --test-dir build -E http-timeout

# Full end-to-end (requires docker):
cd tests/integration && docker compose up -d && cd -
OTLP_C_RUN_INTEGRATION=1 ctest --test-dir build -L integration
docker compose logs otelcol | grep integration_requests_total
docker compose logs otelcol | grep "integration log body"
```
