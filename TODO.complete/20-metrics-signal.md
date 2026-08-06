# TODO 20 — Add OTLP metrics signal (POST /v1/metrics)

**Status:** Complete
**Priority:** P1
**Depends on:** nothing

## Goal

Implement Counter, Histogram, Gauge metric types with full OTLP wire encoding.

## What shipped (v0.4)

**Public API** (`include/otlp-c/metric.h`):
- `otlp_metric_t` opaque type.
- `otlp_metric_type_t` enum: `OTLP_METRIC_COUNTER`, `OTLP_METRIC_GAUGE`, `OTLP_METRIC_HISTOGRAM`.
- `otlp_metric_create()` / `otlp_metric_free()` lifecycle.
- `otlp_metric_record(value)` — counter accumulates, gauge replaces, histogram buckets.
- Time setters: `set_start_time`, `set_time`, `mark_time`.
- Attribute setters: string / int / double (cap 128).

**Internal layout** (`src/metric_internal.h`, `src/metric.c`):
- Counter: single accumulating double.
- Gauge: single double.
- Histogram: count + sum + min + max + sorted bounds + n_bounds+1 bucket counts.
- 128-attribute fixed cap (matches spans).

**Wire encoder** (`src/otlp_metrics_encoder.c`):
- `otlp_encode_export_metrics_service_request()` produces ExportMetricsServiceRequest bytes.
- Envelope: ResourceMetrics → Resource{service.name} + ScopeMetrics → Scope + Metric[].
- Reuses shared helpers from `otlp_messages.c`: `otlp_emit_resource`,
  `otlp_emit_instrumentation_scope`, `otlp_encode_key_value`.
- Sum emitted with `aggregation_temporality=CUMULATIVE` + `is_monotonic=true`.
- Histogram packed `bucket_counts` (fixed64) and `explicit_bounds` (double).

**Shared helpers extracted** from `otlp_messages.c`:
- `otlp_encode_any_value` (was static).
- `otlp_emit_resource` (was static).
- `otlp_emit_instrumentation_scope` (was static).
- Exposed via `src/otlp_messages.h` so metrics/logs encoders compose them.

**Property tests** (`tests/property/test_property_metrics.c`):
- `prop_metrics_empty_request` — no metrics → 0 bytes.
- `prop_metrics_counter_field_nums` — counter envelope shape.
- `prop_metrics_gauge_field_nums` — gauge → as_double{4}.
- `prop_metrics_histogram_field_nums` — histogram fields + packed arrays.
- `prop_metrics_counter_value` — encoded as_double == recorded value (200 iters).
- `prop_metrics_attributes_roundtrip` — int attr round-trips (200 iters).

## Acceptance criteria
- [x] CI green on all platforms (build verified locally on macOS arm64).
- [x] No regression in existing tests.
- [x] Property tests pass deterministically.

## Out of scope (deferred)
- ExponentialHistogram (field 10).
- Summary (field 11).
- Dynamic attribute arrays (current cap: 128).
- Multiple data points per metric (single data point per metric in v0.4).
