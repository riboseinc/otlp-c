# TODO 118 — Resource attributes are a map too

**Status:** Complete (v0.5.78)
**Priority:** P1 (spec compliance: duplicate Resource KeyValues on the wire)

## What shipped

**Problem (found by the v0.5.78 resource-attribute audit):** the
five in-object attribute surfaces got map semantics in v0.5.73,
but resource attributes — set once from
`otlp_exporter_opts_t.resource_attributes` — were copied verbatim
at `otlp_exporter_create`. Two ways to emit non-compliant wire
data (OTLP data model: attribute keys MUST be unique):

1. Duplicate keys in the opts array → duplicate `KeyValue`s in
   every batch's Resource, forever.
2. A caller-supplied `service.name` resource attr collides with
   the auto-emitted `service.name` (the encoder always emits the
   dedicated `service_name` opt first) → a duplicated
   `service.name` in every Resource.

**Fix (at exporter-create, where the opts enter the library):**
duplicate keys collapse last-write-wins, and a `service.name`
entry is dropped when the dedicated `service_name` opt is set (the
documented field wins; when the opt is unset the attrs entry
survives and is emitted as-is). O(n²) over the opts array —
config-time only, trivially small.

Two latent details hardened along the way:

- A NULL key in opts now fails create explicitly (previously it
  failed via the `otlp_dup_str(NULL)` path — same outcome, now
  obvious).
- The replace path NULLs the slot's value before duplicating the
  new one, so the exporter-free path stays safe if the dup fails
  mid-replace.

**Also:** `src/exporter_internal.h` (new, test-only, following the
`otlp_span_struct_size` precedent) exposes
`otlp_exporter_get_resource_attrs` so tests can assert the
normalized array directly — the encoder is already property-tested
to emit exactly what it is handed, so asserting the stored array
pins the dedup precisely.

## Tests

- `prop_resource_attrs_dedup` — duplicate `host.name` entries
  (string then int64) + a distinct `zone`: stored array has 2
  entries, `host.name` is INT64 77 (last write wins, type
  changes).
- `prop_resource_service_name_wins` — attrs with `service.name` +
  dedicated opt set: no `service.name` in the stored array, `zone`
  survives.

Both in `tests/property/test_property_resource_attrs.c` (which
gained exporter.h + exporter_internal.h includes).

## Sites changed

- `src/exporter.c` — dedup loop in `otlp_exporter_create`; test
  accessor.
- `src/exporter_internal.h` — new.
- `include/otlp-c/exporter.h` — `otlp_resource_attr_t` docstring
  documents the map semantics and the service.name precedence.
- `tests/property/test_property_resource_attrs.c` — 2 properties.

## Verification

```
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout          # 36/36
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -E "http-timeout|url-parse"
```
