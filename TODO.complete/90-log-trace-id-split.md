# TODO 90 — LogRecord trace_id / span_id independent emission

**Status:** Complete (v0.5.50)
**Priority:** P1 (correctness: invalid W3C output)

## What shipped

`otlp_log_record` used a single `has_trace` flag set by either
`set_trace_id` or `set_span_id`. The encoder unconditionally
emitted both fields when `has_trace` was true. If the caller set
only one ID, the encoder emitted the unset one as all-zero bytes.

For trace_id specifically, all-zero is invalid per W3C Trace
Context §3.1. Spec-compliant collectors that validate this
constraint would reject or misroute the log record.

Fix: split the single flag into two independent flags
(`has_trace_id`, `has_span_id`). The encoder emits each only
when its own flag is set.

## Sites changed

- `src/log_internal.h` — `has_trace` field → two fields; added
  `otlp_log_has_trace_id`, `otlp_log_has_span_id` accessors.
- `src/log.c` — setters set only their own flag; clone copies
  both; `otlp_log_has_trace` returns OR of the two (preserves
  the public accessor's existing semantics).
- `src/otlp_logs_encoder.c::emit_log_record` — emits each field
  only when its flag is set.
- `tests/property/test_property_logs.c` — added
  `prop_logs_trace_id_only_no_zero_span_id` regression.

## Public ABI

Unchanged. `otlp_log_has_trace` is the only external accessor
and its semantics are preserved (returns "any trace correlation
present"). The new flags are internal.

## Why this matters

The previous design assumed callers set both IDs together, which
matches the typical use case (a log emitted within a traced
request). But the API allows setting them independently:

```c
otlp_log_record_set_trace_id(lr, trace_id);   /* but no span_id */
```

Before v0.5.50, this emitted trace_id correctly plus 8 zero
bytes for span_id. The zero bytes are valid proto `bytes` but
violate W3C for trace_id (forbidden) and are semantically
nonsensical for span_id (zero span_id means "no parent").

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 42/42 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

## Audit context

Continues the encoder / correctness audit:
- v0.5.48: OTLP schema field numbers (Event, Status, NDP, HDP).
- v0.5.49: ExponentialHistogram wire types.
- v0.5.50: LogRecord trace_id / span_id independence.

The audit pattern (cross-check against opentelemetry-proto +
W3C specs) keeps surfacing real correctness bugs. Next likely
targets: span encoder's parent_span_id emission, W3C traceparent
format validation in context.c.
