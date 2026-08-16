# TODO 113 — Attributes are a map: last-write-wins upsert

**Status:** Complete (v0.5.73)
**Priority:** P1 (spec compliance: duplicate keys on the wire)

## What shipped

**Problem:** every attribute setter appended unconditionally. The
public API could produce `[(k=1), (k=2)]`, but:

- the **OTLP data model** says attribute keys **MUST be unique** —
  duplicate KeyValues are non-compliant wire data that receivers
  may drop, mis-aggregate, or keep only one of (behavior is
  unspecified);
- the **OTel API** defines attributes as a map where setting an
  existing key **overwrites** (last write wins).

So a perfectly natural caller pattern (set a default, refine it
later) generated invalid data.

**Fix (upsert folded into the shared reserve):**
`otlp_attr_list_reserve` now searches for the key first — an
existing key's slot is reused with its old value released (count
unchanged, position preserved, type may change); only new keys
append. Overwriting an existing key succeeds even at cap; a new
key past cap still returns `OTLP_ERR_OVERFLOW`. Because reserve
owns the count, one implementation gives all 25 setters
(5 span-level + 5 event + 5 link + 5 metric + 5 log) upsert
semantics with zero per-setter duplication.

**Simplification that fell out:** the reserve-then-fill contract
was reordered to *fill cannot fail* — string/bytes setters now
duplicate the value BEFORE reserving and free the copy if reserve
itself fails. This deletes the fragile "free the slot's key and
NULL it" partial-failure cleanup from every owned-value setter
(the slot is never half-initialized).

**New primitive:** `otlp_attribute_release_value` (payload only,
key kept) extracted from `otlp_attribute_free` — the model-level
operation for replace-value-in-place. `otlp_attribute_free` is now
release_value + free(key).

**Performance:** the linear key search is O(distinct keys) per
set — worst case 128 strcmps against the 128-key cap, unchanged
benchmarks within noise (~1.5μs/span, attrs=5).

## Behavior change (0.x, documented in CHANGELOG)

Previously duplicate keys appended; now they overwrite. Callers
relying on append-with-duplicates (nobody sane) would see
different wire output. All existing tests passed unchanged.

## Tests

- `prop_attr_upsert_last_write_wins` (1000 iters): re-setting a
  key replaces value + type in place, count and slot position
  unchanged, other attributes untouched.
- `prop_attr_upsert_at_cap`: at cap a new key overflows but
  overwriting an existing key succeeds, count stays 128.
- `prop_attr_upsert_wire_identical` (1000 iters): a span whose key
  was set twice encodes **byte-identically** to one whose key was
  set once with the final value — proving no duplicate KeyValue
  reaches the wire without needing a wire walker.
- Unit upsert tests in all three signals (span/metric/log):
  18/10/11 tests.

## Sites changed

- `src/internal_util.{h,c}` — `otlp_attr_list_find`,
  `otlp_attribute_release_value`, upsert reserve + contract docs.
- `src/span.c` — `attr_reserve` (inline array) upsert; 15 setters
  simplified.
- `src/metric.c`, `src/log.c` — 10 setters simplified.
- `include/otlp-c/{span,metric,log}.h` — attribute doc comments
  state the map semantics.
- `tests/property/test_property_attribute_roundtrip.c` — 3 new
  properties.
- `tests/unit/test_unit_{span,metric,log}.c` — upsert tests.

## Verification

```
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout          # 36/36
OTLP_C_PROPERTY_ITERS=20000 ctest --test-dir build -R "property-attr|property-span|property-logs|property-metrics"
cmake --build build-asan && ctest --test-dir build-asan -E http-timeout   # clean
./build/bench/otlp_bench_emit && ./build/bench/otlp_bench_logs  # unchanged
```
