# TODO 101 — ExponentialHistogram schema entry (DRY/MECE)

**Status:** Complete (v0.5.61)
**Priority:** P2 (architecture: schema completeness)

## What shipped

The `ExponentialHistogram` message had no schema entry. The
encoder emitted its `aggregation_temporality` (field 2) using
`HIST_F_AGG_TEMP` from the `Histogram` message — which works
because both messages have `aggregation_temporality` at field 2,
but is a DRY/MECE violation.

Fix: added `OTLP_EH_FIELDS` with `data_points = 1` and
`aggregation_temporality = 2`. The encoder now uses
`EH_F_AGG_TEMP` for `ExponentialHistogram`.

## Sites changed

- `src/otlp_schema.h` — added `OTLP_EH_FIELDS` enum and table.
- `src/otlp_metrics_encoder.c` — added `EH_F_AGG_TEMP` macro;
  `emit_kind_extra_fields` now uses it for
  `OTLP_METRIC_EXP_HISTOGRAM`.
- `tests/property/test_property_metrics.c` —
  `prop_metrics_exp_histogram_field_nums` now verifies
  `aggregation_temporality` at field 2 of the ExponentialHistogram
  wrapper (before descending into the data point).

## Why

The schema is the single source of truth for field numbers. Each
OTLP message should have its own entry, even when fields happen
to be identical to another message's. The reused
`HIST_F_AGG_TEMP` was a correctness landmine — coincidentally
correct today, fragile if Histogram's fields ever diverge from
ExponentialHistogram's in a future proto revision.

## Same class as v0.5.48

v0.5.48 added missing schema entries and fixed wrong field
numbers for Event, Status, NumberDataPoint, HistogramDataPoint.
v0.5.61 completes the schema for ExponentialHistogram (the
wrapping message — its data point schema was already present).

## Schema completeness now

Every OTLP message used by the library has its own schema entry:
- ExportTraceServiceRequest, ExportMetricsServiceRequest,
  ExportLogsServiceRequest.
- ResourceSpans, ResourceMetrics, ResourceLogs.
- ScopeSpans, ScopeMetrics, ScopeLogs.
- Resource, InstrumentationScope.
- Span, Event, Link, Status.
- KeyValue, AnyValue, ArrayValue, KeyValueList.
- Metric.
- Sum, Gauge, Histogram, **ExponentialHistogram**.
- NumberDataPoint, HistogramDataPoint,
  ExponentialHistogramDataPoint, ExponentialHistogramBuckets.
- LogRecord.

No message is missing or reuses another's schema entry.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 34/34 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```
