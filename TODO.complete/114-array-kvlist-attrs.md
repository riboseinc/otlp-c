# TODO 114 — ARRAY/KVLIST attributes end-to-end (+ encoder fix)

**Status:** Complete (v0.5.74)
**Priority:** P1 (feature completion + a latent wire-format bug)

## What shipped

The attribute model had composite types — `OTLP_ATTR_ARRAY` /
`OTLP_ATTR_KVLIST` with structs, recursive free, and encoder
dispatch — but no way to create one: no setters, and clone refused
them (`otlp_attribute_copy_all` failed on ARRAY/KVLIST). This
release closes the gap end-to-end.

**Public API** — a new `include/otlp-c/value.h` introduces the
borrowed-data scalar value type:

- `otlp_value_t` — tagged union (string/bool/int64/double/bytes)
- `otlp_kv_t` — one KeyValueList entry

and ten setters take flat arrays of them (deep-copied; the caller
keeps ownership and the inputs are flat — nesting is not
expressible through the public API, by design):

- `otlp_span_set_attribute_array` / `_kvlist`
- `otlp_span_set_event_attribute_array` / `_kvlist`
- `otlp_span_set_link_attribute_array` / `_kvlist`
- `otlp_metric_set_attribute_array` / `_kvlist`
- `otlp_log_record_set_attribute_array` / `_kvlist`

All with the v0.5.73 upsert semantics (re-setting the key replaces
the whole tree).

**Build-then-attach:** composite values are multi-allocation, so
they can't follow the fill-cannot-fail contract directly. The
setters build the complete owned tree first
(`otlp_attr_array_build` / `otlp_attr_kvlist_build` in
internal_util), then reserve (freeing the tree if reserve fails),
then attach with two non-failing assignments.

**Clone:** `otlp_attribute_copy_all` now deep-copies recursively —
rewritten as a per-item `attr_copy_one` that handles all seven
types, including nested trees, with partial-failure cleanup at
every level.

## The bug the tests caught

Writing `prop_attr_array_wire` (walk the wire into the nested
oneof) failed against the *pre-existing* encoder. Hex-dumping the
frame showed it: **`encode_attr_array` / `encode_attr_kvlist` wrote
their body without the outer LEN prefix.** `otlp_encode_any_value`
writes the tag; scalar encoders write their own length+value; but
the composite encoders wrote the items directly — a malformed
frame (the first item's tag was misread as the array length).

Nobody ever noticed because no code path could produce an
ARRAY/KVLIST attribute to encode. Both encoders now build the body
into a temp buffer and emit `LEN + body` (`otlp_pb_bytes`),
matching the scalar encoders' contract. This is the same class of
blind spot the v0.5.48 Event field swap had: code that no test
could reach.

## Tests

- `unit-span` (19), `unit-metric` (11), `unit-log` (12): composite
  roundtrips, upsert-over-composite (tree released), clone
  deep-copy of nested trees.
- `prop_attr_array_wire` (1000 iters): walks
  attributes{9} → value{2} → array_value{5} → values{1} items,
  verifying each item's oneof member and value on the wire.
- `prop_attr_kvlist_wire` (1000 iters): walks to
  kvlist_value{6} → values{1} → KeyValue key{1} + value{2}.
- ASAN + the fail-injecting OOM sweep clean (clone now allocates
  recursively; every offset probed).

## Sites changed

- `include/otlp-c/value.h` — new; `otlp.h` umbrella + the three
  signal headers include it.
- `src/internal_util.{h,c}` — `otlp_attr_array_build` /
  `_kvlist_build` / `_array_free` / `_kvlist_free`; recursive
  `attr_copy_one` rewrite of `otlp_attribute_copy_all`.
- `src/otlp_messages.c` — the LEN-prefix fix in both composite
  encoders.
- `src/span.c`, `src/metric.c`, `src/log.c` — ten setters.
- `tests/` — unit + property coverage above.

## Verification

```
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout          # 36/36
cmake --build build-asan && ctest --test-dir build-asan -E http-timeout
```
