# TODO 89 — ExponentialHistogram wire-format fixes

**Status:** Complete (v0.5.49)
**Priority:** P0 (correctness: wire-format compatibility)

## What shipped

Continued audit of the metrics encoder against upstream
`opentelemetry-proto`. Found two wire-type bugs in the
ExponentialHistogram path:

1. **`ExponentialHistogramDataPoint.zero_count`** — schema said
   VARINT, upstream says FIXED64. Real values would be skipped
   by spec-compliant decoders as a wire-type mismatch.

2. **`ExponentialHistogram.Buckets.bucket_counts`** — encoder
   packed entries as fixed64 (8 bytes each). Upstream declares
   this as `repeated uint64` (varint), explicitly for varint
   compression of small/sparse counts. Decoders would parse
   the LEN payload as concatenated varints and produce garbage.

## Sites changed

- `src/otlp_schema.h` — `OTLP_EHDP_FI_ZERO_COUNT` wire type:
  VARINT → FIXED64.
- `src/otlp_metrics_encoder.c::emit_exp_histogram_data_point` —
  `otlp_pb_field_varint(... zero_count ...)` →
  `otlp_pb_field_fixed64(...)`.
- `src/otlp_metrics_encoder.c::emit_exp_histogram_buckets` —
  inner loop `otlp_pb_fixed64(&packed, counts[i])` →
  `otlp_pb_varint(&packed, counts[i])`. Comment updated to
  note that bucket_counts here is `repeated uint64` (varint),
  different from `HistogramDataPoint.bucket_counts` which is
  `repeated fixed64`.
- `tests/property/test_property_metrics.c` — added
  `prop_metrics_exp_histogram_field_nums` regression.

## Why

Same audit pattern as v0.5.48: schema and encoder had not been
comprehensively cross-checked against upstream proto declarations.
The two bugs together meant ExponentialHistogram data points
arrived at the collector with:
- zero_count silently dropped (wire-type mismatch).
- bucket_counts parsed as a single garbage varint instead of N
  small varints — the high bytes of the first 8-byte fixed64
  have MSB set, looking like varint continuation bytes.

Net: every ExponentialHistogram metric was unusable on
spec-compliant collectors before this fix.

## Test gap

`prop_flush_exp_histogram_with_buckets` (existing) only checks
that `flush_metric` returns OK under null_transport. It doesn't
decode the wire. The new `prop_metrics_exp_histogram_field_nums`
decodes the wire and verifies the post-fix format end-to-end.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 41/41 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

## Audit context

Continues the schema-and-encoder audit:
- v0.5.31-v0.5.33: clone functions (span/metric/log attributes).
- v0.5.41-v0.5.42: emit lifecycle (move-variant shutdown leak,
  clone-variant symmetry).
- v0.5.47: copy_all fail path + HTTP no-CL parser.
- v0.5.48: OTLP schema field numbers (Event, Status, NDP, HDP).
- v0.5.49: ExponentialHistogram wire types.

Next audit target: the logs encoder body field (AnyValue-typed)
and the Span encoder's trace_id / parent_span_id handling.
