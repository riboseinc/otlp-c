# TODO 165 — Per-signal endpoints (the model change)

**Status:** Complete (v0.7.3)
**Priority:** P1 (user-confirmed: "per-signal metric/log endpoint
env vars are worth the model change")

## The model change

One full URL per signal (sig[s].url) replaces the single traces
URL + path rewrites:

- SIGNAL_SPECS gains a default_path column (/v1/traces,
  /v1/metrics, /v1/logs) — the add-a-signal table now owns the
  path too.
- create(): sig[s].url = signal-specific opt (new
  metrics_endpoint / logs_endpoint, public + additive) when set,
  else endpoint base (scheme+host+port) + spec default_path.
- exporter_otel build fns take the signal's URL verbatim (the
  snprintf path rewrites deleted); try_start_post passes
  &e->sig[s].url.
- Sync flush: flush_sync takes the signal id and uses
  sig[signal].url — the path parameter (and its "path + 5"
  comment) is gone.

## Env vars

- OTEL_EXPORTER_OTLP_METRICS_ENDPOINT / _LOGS_ENDPOINT: full
  forms, win per signal (storage: two more endpoint buffers).
- OTEL_EXPORTER_OTLP_ENDPOINT is now a true BASE: each signal's
  default path appended; a value carrying its own path is
  stripped to scheme+host+port (exact control = signal forms).
  Behavior change from v0.7.0-2 documented in CHANGELOG.

## Verification

- unit-env-config: per-signal composition, base-with-path
  stripping, independent overrides.
- exporter-echo wire test: flush_metric through a custom
  metrics_endpoint; the captured request asserts
  "POST /custom-metrics HTTP/1.1".
- 52/52 via default/release/asan/ubsan/tsan presets.
