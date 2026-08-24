# TODO 163 — OTEL_RESOURCE_ATTRIBUTES + bench/coverage presets + last legacy

**Status:** Complete (v0.7.1)
**Priority:** P1 (feature completion) + P2 (build ease, legacy)

## Feature: OTEL_RESOURCE_ATTRIBUTES

The last OTel standard variable that maps cleanly onto existing
opts. `otlp_env_apply_resource_attrs` parses "k=v,k=v": split on
commas, first '=' per segment; empty segments / no-'=' / empty
keys skipped; values literal (no URL-decoding, documented);
pairs become STRING resource attributes on the one value model;
opts->resource_attributes points into storage.
"service.name" yields to OTEL_SERVICE_NAME / the service_name
opt via the existing create-time map semantics (matches the OTel
precedence rule).

**API shape fix**: v0.7.0's `apply_env(opts, char *buf, cap)`
became `apply_env(opts, otlp_env_storage_t *storage)` — one
caller-allocated struct (endpoint + 32 key/value pairs + the
attrs array) so future variables extend storage without another
signature change. Documented 0.x change on a one-day-old API.

## Build ease + perf

- `cmake --preset bench` (Release + benchmarks) and
  `--preset coverage` join the five existing presets.
- Release perf measured via the bench preset: **emit 89 ns/span**
  (0 attrs; 272 ns @5 attrs), build+move 135 ns, encode
  360 ns + ~60 ns/attr — ~2x the Debug numbers, linear in attrs.

## Legacy deleted

- docs/release-notes/ (frozen at v0.5.0 since v0.6.8): the
  v0.5.0.md content was merged into the v0.5.0 GitHub release
  body BEFORE deletion — nothing lost; recoverable from history.

## Verification

52/52 via default/release/asan/ubsan/tsan presets; Doxygen zero
warnings; new parser cases (multi-pair, malformed, '=' in value,
empty key/value, pair-cap overflow, exporter round trip) green.
