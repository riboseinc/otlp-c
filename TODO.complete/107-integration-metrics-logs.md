# TODO 107 — Integration test covers all three signals + sync-flush retry

**Status:** Complete (v0.5.67)
**Priority:** P1 (end-to-end validation) + P2 (sync-path resilience)

## What shipped

1. **Integration test emits a metric + a log record.** A counter
   with an attribute via `flush_metric`; a log record with
   severity/body/attribute via `flush_log`. Both assert a 2xx
   from otelcol — the collector's real protobuf parser accepted
   the `ExportMetricsServiceRequest` / `ExportLogsServiceRequest`.

2. **otelcol config gains metrics + logs pipelines** with the
   `debug` exporter (verbosity: detailed — the default `basic`
   prints only counts, which are not grep-able).

3. **Two new CI steps grep otelcol logs** for the metric name and
   log body, confirming they arrived intact.

4. **Sync flush retry (library fix, found by the new test).**
   `flush_sync` had no retry — a transient connect failure
   failed the flush immediately. The CI's first metrics POST
   hit exactly this. Now retries pre-response network failures
   with the exporter's `max_retries` budget (100ms backoff);
   non-2xx responses and timeouts remain permanent.

5. **Diagnostic logging on sync flush paths (library fix).**
   `flush_metric`/`flush_log` failures were silent — the caller
   got `OTLP_ERR_NETWORK` with no HTTP status or error detail.
   `flush_sync` now emits diagnostic-callback events at each
   failure mode, closing a gap in the v0.5.23 callback coverage.
   The integration test installs a logger that prints them.

## Sites changed

- `tests/integration/otelcol-config.yaml` — debug exporter +
  metrics/logs pipelines.
- `tests/integration/test_integration_jaeger.c` — emit metric +
  log; install diagnostic logger.
- `.github/workflows/ci.yml` — two grep verification steps.
- `src/exporter.c` — `flush_post_once` extracted from
  `flush_sync`; retry loop for transient failures; `otlp_log`
  calls on all failure modes.

## Debugging journey (why the extra library fixes)

The CI integration job failed twice before passing:
1. First failure: "flush_metric returned -10" — no detail
   available because sync flush failures were silent. Added the
   diagnostic logging.
2. Second failure with diagnostics: "request failed (network)" —
   a transient connect failure, identical to the `network_err=1`
   visible in the v0.5.66 trace stats (the async path had
   silently retried past it). Added the retry.
3. Third failure: metric grep found nothing — `verbosity: basic`
   prints counts only. Switched to `detailed`.
4. Fourth run: all green.

The integration test found two real library gaps on its first
extended run — exactly the payoff it was built for.

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
