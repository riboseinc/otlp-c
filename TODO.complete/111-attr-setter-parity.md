# TODO 111 — Attribute setter type parity across signals

**Status:** Complete (v0.5.71)
**Priority:** P2 (API completeness: metrics/logs were missing types the span has)

## What shipped

**Problem:** the three signals had asymmetric attribute-type coverage:

| type | span | metric | log |
|---|---|---|---|
| string | yes | yes | yes |
| int64 | yes | yes | yes |
| double | yes | yes | **no** |
| bool | yes | **no** | **no** |
| bytes | yes | **no** | **no** |

A caller tagging metrics with a boolean flag (e.g. `canary=true`) or
log records with binary context (e.g. a frame digest) had no way to
express it — they'd have to stringify, losing type fidelity on the
wire (AnyValue bool_value{2} / bytes_value{7} vs string_value{1}).
The storage (`struct otlp_attribute` union), clone path
(`otlp_attribute_copy_all`), and encoders (`otlp_encode_any_value`
dispatches on `attr->type` via the schema table) already supported
every type — only the setters were missing.

**Fix:** five new public setters, each a thin call through the
v0.5.70 `otlp_attr_list_reserve` helper (the OCP payoff of that
refactor: no new storage-model code, just typed value fills):

- `otlp_metric_set_attribute_bool`
- `otlp_metric_set_attribute_bytes`
- `otlp_log_record_set_attribute_double`
- `otlp_log_record_set_attribute_bool`
- `otlp_log_record_set_attribute_bytes`

The bytes variants follow span's exact contract: `NULL` + `len==0`
is an empty value; `len > 0` with `NULL` is `OTLP_ERR_NULL`; the
payload is copied (caller keeps ownership).

Also: `log.h` was missing `#include <stdbool.h>` (the new `bool`
parameter surfaced it — the header never used the type before);
added.

## Tests

- `unit-metric` (7→9): bool + bytes roundtrips; clone test now
  also deep-copies a bytes attribute (asserts the copy is a
  distinct pointer with equal contents).
- `unit-log` (7→10): double + bool + bytes roundtrips; clone test
  extended the same way.
- `prop_metrics_attributes_roundtrip` /
  `prop_logs_attributes_roundtrip` upgraded from a fixed int64
  attribute to cycling int64/double/bool/bytes by seed. Each
  iteration walks the wire to the AnyValue and asserts the oneof
  member's field number + wire type (bool{2} VARINT, int64{3}
  VARINT, double{4} FIXED64, bytes{7} LEN) and the exact encoded
  value — verifying end-to-end that the new setters produce
  spec-correct bytes. Verified at `OTLP_C_PROPERTY_ITERS=20000`.

## Sites changed

- `include/otlp-c/metric.h`, `include/otlp-c/log.h` — declarations
  (+ stdbool in log.h).
- `src/metric.c`, `src/log.c` — implementations.
- `tests/unit/test_unit_metric.c`, `tests/unit/test_unit_log.c` —
  new cases.
- `tests/property/test_property_metrics.c`,
  `tests/property/test_property_logs.c` — type-cycling roundtrip.

No encoder, schema, clone, or free-path changes were needed — the
existing model-driven dispatch already covered the types.

## Verification

```
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout          # 36/36
OTLP_C_PROPERTY_ITERS=20000 ctest --test-dir build -R "property-logs|property-metrics"
cmake --build build-asan && ctest --test-dir build-asan -E http-timeout   # clean
```
