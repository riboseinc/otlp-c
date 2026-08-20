# TODO 132 — Resource attributes on the one value model

**Status:** Complete (v0.5.92)
**Priority:** P1 (API unification; breaking change within the 0.x policy)

## What shipped

Resource attributes were the last surface on a parallel-fields
struct (`value`/`int64_val`/`double_val`/`bool_val`, 4 of 7 types).
They now use the ONE public value model:

```c
typedef struct {
    const char   *key;
    otlp_value_t value;   /* all AnyValue types */
} otlp_resource_attr_t;
```

**Migration:** `.type = OTLP_RESOURCE_ATTR_INT64, .int64_val = 5`
becomes `.value = {.type = OTLP_VALUE_INT64, .v = {.int64_val = 5}}`.

Internally the exporter now stores resource attributes as an
owned `struct otlp_attr_vec` of internal attributes, built by the
set-attribute engine at create time — so map semantics (dedup,
service.name precedence, empty-string skip) and deep copy are the
engine's, not a re-implementation. The encoder's 4-way type switch
in `otlp_emit_resource` is deleted: encoding goes through the one
`otlp_encode_any_value` dispatch, and BYTES resource attributes
(now supported) work with zero encoder changes — OCP in practice.

## Breaking change

The struct shape changed (0.x policy: breaking changes allowed
between minor versions, documented here and in CHANGELOG).

## Tests

- All 10 resource-attr properties converted to the new model and
  passing, plus `prop_resource_full_value_model` (BYTES
  round-trip through the exporter).
- OOM-injection sweep over exporter create re-verified; found and
  fixed a **test-infrastructure accounting bug**: `fail_realloc`
  modeled realloc as alloc-only, so any grow-on-demand pattern
  (the attribute vectors since v0.5.75) showed a phantom +1 leak
  per growth. Realloc(p≠NULL) now counts free+alloc. (LSAN
  confirmed no real leak — the mismatch was pure accounting.)

## Verification

39/39 Debug + Release; ASAN + LeakSanitizer 38/38; examples
rebuild and run.
