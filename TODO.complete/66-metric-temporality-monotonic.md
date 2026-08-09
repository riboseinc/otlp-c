# TODO 66 — Configurable metric aggregation temporality + is_monotonic

**Status:** Complete (v0.5.26)
**Priority:** P1 (spec compliance)

## What shipped (v0.5.26)

### Fixed: aggregation_temporality was always CUMULATIVE

The OTLP encoder hardcoded `aggregation_temporality = CUMULATIVE`
for Counter (Sum), Histogram, and ExponentialHistogram.

The header defined both values:
```c
#define OTLP_AGG_TEMP_DELTA      1
#define OTLP_AGG_TEMP_CUMULATIVE 2
```

But the metric struct didn't store a temporality field, and the
encoder never consulted it. Callers who needed DELTA temporality
(push-based delta reporting — common in Prometheus-style
scraping, where each export reports the delta since the last
scrape) had no way to set it.

**Fix:**
1. Added `uint8_t agg_temp` to `struct otlp_metric` (default
   CUMULATIVE).
2. Added `otlp_metric_set_aggregation_temporality(m, temp)` —
   validates temp is DELTA or CUMULATIVE; rejects UNSPECIFIED.
3. `emit_kind_extra_fields` reads from the struct instead of
   hardcoding.

### Fixed: is_monotonic was always true for Counter

The encoder hardcoded `is_monotonic = true` for Counter (Sum).
Callers who needed an up/down counter (queue depth, active
connections, circuit breaker state — metrics that can decrease)
had no way to set `is_monotonic = false`.

**Fix:**
1. Added `bool is_monotonic` to `struct otlp_metric` (default
   true).
2. Added `otlp_metric_set_monotonic(m, bool)`.
3. Encoder reads from the struct.

**Proto3 semantics:** when `is_monotonic = false`, the encoder
emits value 0 via `otlp_pb_field_varint`, which skips zero
values per the proto3 "empty fields omitted" convention. The
collector interprets the ABSENCE of the field as false. This is
correct — the test verifies field 3 is absent, not present with
value 0.

### Property tests (2 new)

`test_property_metrics.c` extended from 6 to 8 properties:

- `prop_metrics_delta_temporality` — encodes a counter with
  DELTA temporality; descends EMSR → RM → SM → Metric → Sum;
  verifies field 2 (agg_temp) is a VARINT with value 1 (DELTA).
- `prop_metrics_non_monotonic_counter` — encodes a counter with
  `is_monotonic = false`; verifies field 3 (is_monotonic) is
  ABSENT (proto3 omits false/default bools; collector interprets
  absence as false).

## Why this matters

The two hardcoded values limited the library to cumulative,
monotonic metrics — a subset of what OTLP supports. Real
use cases that need DELTA temporality or up/down counters
were impossible.

- **DELTA temporality**: used by push-based exporters that
  compute deltas between scrapes (e.g., "requests in the last
  60 seconds" rather than "total requests since start"). Without
  this, backends that expect delta-format metrics misinterpret
  the data.
- **is_monotonic=false**: used for counters that can decrease.
  Queue depth, active connections, semaphore counts — all are
  "Sum" metrics but non-monotonic. Without this, backends
  assume the counter only goes up and produce incorrect rate
  calculations.

## Design (OCP + model-driven)

The fix follows the project's invariants:

- **OCP**: existing API unchanged; new functionality is two new
  setters. Existing callers get CUMULATIVE + true (defaults).
- **Model-driven**: the values match OTLP proto enum values
  exactly (DELTA=1, CUMULATIVE=2). The encoder reads from the
  struct via accessors, not from hardcoded constants.
- **MECE**: the metric struct is the single source of truth for
  a metric's encoding parameters. The encoder is a pure
  function of the struct.
- **DRY**: `emit_kind_extra_fields` reads both values from one
  metric pointer; no duplication across metric types.

## Acceptance criteria
- [x] `agg_temp` field on `struct otlp_metric` (default CUMULATIVE).
- [x] `is_monotonic` field on `struct otlp_metric` (default true).
- [x] `otlp_metric_set_aggregation_temporality` validates input.
- [x] `otlp_metric_set_monotonic` setter.
- [x] Encoder reads from struct (not hardcoded).
- [x] DELTA temporality appears on wire as field 2 = VARINT 1.
- [x] is_monotonic=false → field 3 absent (proto3 convention).
- [x] 32/32 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
