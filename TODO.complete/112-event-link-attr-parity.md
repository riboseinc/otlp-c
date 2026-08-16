# TODO 112 — Event/link attribute type parity

**Status:** Complete (v0.5.72)
**Priority:** P2 (API completeness: events/links were string-only)

## What shipped

**Problem:** v0.5.71 gave metrics and log records the full
string/int64/double/bool/bytes attribute set, but one level down the
span API was still asymmetric: `otlp_span_set_event_attribute_string`
and `otlp_span_set_link_attribute_string` were the *only* event/link
attribute setters. An event like `cache_miss` couldn't carry
`attempts=3` as an int64; a link couldn't carry binary context.
Callers had to stringify, losing AnyValue type fidelity.

**Fix:** eight new public setters —

- `otlp_span_set_event_attribute_{int,double,bool,bytes}`
- `otlp_span_set_link_attribute_{int,double,bool,bytes}`

Each targets the most-recently-added event/link (same contract as
the string variant: `OTLP_ERR_INVALID_ARGUMENT` before any
add_event/add_link, 32-attribute cap, lazy array via
`otlp_attr_list_reserve`).

**DRY:** the two existing setters had a duplicated
validate-target-reserve prelude. With ten setters that would have
been ten copies, so the prelude moved into static
`event_attr_slot` / `link_attr_slot` helpers — each public setter
is now reserve + typed fill + count increment.

**Interesting find while writing the wire test:** the first draft
of `prop_event_link_typed_attrs_wire` assumed event and link
attributes shared a field number and failed. Hex-dumping the wire
showed why: **Event.attributes is field 3** (time=1, name=2,
attributes=3) while **Link.attributes is field 4** (trace_id=1,
span_id=2, trace_state=3, attributes=4, dropped=5, flags=6). The
encoder and schema were already correct — the test now pins both
field numbers explicitly, so a future regression in either table
fails a test that names the exact spec fields.

## Tests

- `unit-span` (14→17): typed event/link attribute roundtrips
  (all four new types on each), plus the
  set-before-add_event/add_link error contract for the new
  variants.
- `prop_span_clone_preserves_evlink_attrs`: now also deep-copies an
  int event attribute and a bytes link attribute (typed coverage,
  not just strings).
- `prop_event_link_typed_attrs_wire` (new): cycles
  int64/double/bool/bytes by seed and walks the wire to the
  AnyValue oneof inside Event{11} (attributes{3}) and Link{13}
  (attributes{4}), asserting field number + wire type + value.

No encoder changes were needed — `otlp_emit_attributes` is
type-driven and already encoded all five types from the lazy arrays.

## Sites changed

- `include/otlp-c/span.h` — 8 new declarations + updated doc
  comments.
- `src/span.c` — `event_attr_slot` / `link_attr_slot` preludes +
  10 setters (2 refactored, 8 new).
- `tests/unit/test_unit_span.c` — 3 new tests.
- `tests/property/test_property_events_context.c` — clone test
  extended; new wire property; file-header doc fixed (it listed
  Event name/time field numbers swapped).

## Verification

```
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout          # 36/36
cmake --build build-asan && ctest --test-dir build-asan -E http-timeout
```
