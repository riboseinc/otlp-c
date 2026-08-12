# TODO 94 — Reject all-zero IDs at set time (W3C §3.1.1 / §3.1.2)

**Status:** Complete (v0.5.54)
**Priority:** P1 (correctness: W3C compliance at the public API)

## What shipped

Five ID setters accepted all-zero IDs without complaint. The bytes
would be emitted on the wire where W3C-compliant receivers reject
them:

- W3C Trace Context §3.1.1: trace-id MUST NOT be all-zero.
- W3C Trace Context §3.1.2: parent-id MUST NOT be all-zero.

The traceparent parser already rejected all-zero (since the
original W3C implementation), but the setters — the public API
applications actually use — did not. A caller could set all-zero
IDs and have them silently propagated to the collector.

Fix: all five setters now return `OTLP_ERR_INVALID_ARGUMENT`
when the input is all-zero.

## Sites changed

- `src/internal_util.{h,c}` — added `otlp_id_is_all_zero` helper.
- `src/span.c` — `otlp_span_set_trace_id`, `_span_id`,
  `_parent_span_id` validate before memcpy.
- `src/log.c` — `otlp_log_record_set_trace_id`, `_span_id`
  validate before memcpy.
- `tests/property/test_property_span.c` — added
  `prop_setters_reject_all_zero_ids`.
- `tests/property/test_property_logs.c` — added
  `prop_logs_setters_reject_all_zero_ids`.

## Behavior change

Callers that previously set all-zero IDs (which were invalid
anyway) now get `OTLP_ERR_INVALID_ARGUMENT`. The change is
intentional: the previous behavior produced malformed wire
output.

To clear a span's parent: `otlp_span_set_parent_span_id(span,
NULL)` is still supported. NULL was always the documented clear
mechanism.

For log records: the v0.5.50 split flags (`has_trace_id`,
`has_span_id`) let callers set just one ID. The all-zero
validation is consistent — setting an ID to all-zero is
meaningless; don't set it.

## Why no public API for clearing trace_id / span_id

There's no use case for clearing trace_id or span_id once set on
a span. The lifecycle is: create span → set IDs → emit → free.
A span never needs to "forget" its ID. Parent ID is different —
a span might initially have a parent, then be promoted to root
in some edge cases. The clear-parent API supports that.

Log records are similar: once correlated to a trace, they stay
correlated. No clear API needed.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 50/50 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

## Audit context

Continues the audit pattern. The previous W3C spec fixes were:
- v0.5.50: LogRecord asymmetric trace correlation.
- v0.5.53: Context propagation CRLF injection.

This release (v0.5.54) closes the W3C ID validation gap in the
public setter API. Combined with v0.5.50 (no zero-fill
emission) and the traceparent parser (no zero-fill on
extraction), the library is now fully W3C-compliant at all
paths where IDs enter or leave the system.

## Next likely targets

- Tracer PRNG seeding (lower priority — OTLP doesn't require
  cryptographic randomness).
- HTTP response parser: chunked transfer encoding handling
  (currently OK for OTLP because the body isn't interpreted,
  but strict spec compliance would decode chunks).
- Resource encoder: typed attribute value-emission consistency
  (string empty-skip vs scalar zero-emit).
