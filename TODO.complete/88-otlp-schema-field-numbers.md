# TODO 88 — OTLP schema field-number audit

**Status:** Complete (v0.5.48)
**Priority:** P0 (correctness: wire-format compatibility)

## What shipped

Cross-checked every schema table in `src/otlp_schema.h` against
upstream `opentelemetry-proto`. Found four field-number bugs:

1. **Event** — schema had `name=1, time=2`. Upstream is
   `time_unix_nano=1, name=2`. Swapped.
2. **Status** — schema had `code=1, message=2`. Upstream is
   `reserved 1; message=2; code=3`. Field 1 is reserved.
3. **NumberDataPoint** — schema had `attributes=1`. Upstream has
   `reserved 1`, `attributes=7`. Field 1 reserved; attributes
   moved to 7.
4. **HistogramDataPoint** — schema had `attributes=1, min=9,
   max=10`. Upstream has `reserved 1`, `attributes=9, min=10,
   max=11`.

Plus three doc fixes in `docs/otlp-spec.md`:
- `Span.flags` proto snippet: `uint32` → `fixed32` (matches
  table and upstream).
- `Link.flags` proto snippet: `uint32` → `fixed32`.
- `Status` proto snippet: `code=1, message=2` →
  `reserved 1; message=2; code=3`.

## Sites changed

- `src/otlp_schema.h` — Event, Status, NumberDataPoint,
  HistogramDataPoint field specs corrected.
- `docs/otlp-spec.md` — Span.flags, Link.flags, Status proto
  snippets aligned with upstream.
- `tests/property/test_property_events_context.c` —
  `prop_events_field_roundtrip` updated to expect time=1, name=2.
- `tests/property/test_property_metrics.c` —
  `prop_metrics_attributes_roundtrip` updated to look for
  attributes at field 7.
- `tests/property/test_property_messages.c` —
  `prop_encode_status_present` extended to descend into Status
  and verify internal field numbers.

## Why so many at once?

The schema was the authoritative source for field numbers but
had never been comprehensively cross-checked against
opentelemetry-proto upstream. The docs were wrong in places, and
the tests — written against the same wrong schema — were
self-consistent with the bugs. Each property test verified that
the encoder matched the schema; none verified that the schema
matched the spec.

Cross-checking each schema table against the corresponding
upstream `.proto` surfaced all four errors in one pass.

## Severity

Each error silently produced invalid OTLP wire output. Whether
collectors recovered depended on which fields were wrong:
- Event swap: collectors saw wire type LEN at field 1 (where
  fixed64 was expected), skipped; same for field 2. Events
  arrived with empty name and timestamp=0.
- Status code: collectors saw wire type VARINT at field 1
  (where nothing is expected — reserved), skipped. Status
  arrived as UNSET regardless of caller intent.
- NumberDataPoint/HistogramDataPoint attributes: collectors saw
  wire type LEN at field 1 (reserved), skipped. Attributes
  arrived empty.

None crashed the pipeline; all lost data.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 40/40 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

## How to verify field numbers in the future

The schema is now (post-v0.5.48) cross-checked against
opentelemetry-proto main. To re-verify after upstream changes:

```sh
for msg in trace/v1/trace metrics/v1/metrics logs/v1/logs \
           common/v1/common resource/v1/resource; do
  gh api /repos/open-telemetry/opentelemetry-proto/contents/opentelemetry/proto/$msg.proto \
    --jq '.content' | base64 -d | grep -E '^[ ]*[a-z].*= [0-9]'
done
```

Compare against `src/otlp_schema.h`. Any mismatch is a bug.

## Audit context

Continues the audit pattern: v0.5.31-v0.5.33 (clones),
v0.5.41-v0.5.42 (emit lifecycle), v0.5.47 (copy_all fail path +
HTTP no-CL), v0.5.48 (OTLP schema field numbers).

Next audit target: encoder correctness for sub-message wrapping
(length-prefixed framing) and varint bounds.
