# TODO 60 — Resource attributes

**Status:** Complete (v0.5.20)
**Priority:** P1 (spec compliance gap)

## The gap

The OTLP Resource message carries arbitrary KeyValue attributes —
`service.name`, `service.version`, `service.namespace`,
`deployment.environment`, `host.name`, `process.pid`, etc. (see
the [semantic conventions](https://opentelemetry.io/docs/specs/semconv/)).

Until v0.5.20, the library only let callers set `service.name` via
`otlp_exporter_opts_t.service_name`. The encoder hardcoded that
one field and dropped everything else on the floor:

```c
/* src/otlp_messages.c — before */
otlp_status_t otlp_emit_resource(parent, field_num, service_name) {
    /* emit service.name as the ONLY attribute */
}
```

A real user wanting `service.version` or `deployment.environment`
on their telemetry had no API to set it. This is a meaningful spec
gap — most production deployments set at least service.version and
deployment.environment, and observability backends use these for
filtering and grouping.

## The fix (v0.5.20)

### Public API addition (`include/otlp-c/exporter.h`)

```c
typedef struct {
    const char *key;
    const char *value;
} otlp_resource_attr_t;

typedef struct {
    /* ... existing fields ... */
    const char *service_name;

    /* NEW */
    const otlp_resource_attr_t *resource_attributes;
    size_t n_resource_attributes;

    /* ... */
} otlp_exporter_opts_t;
```

**Design decisions:**

- **String-only for v0.5.x.** Every common Resource attribute
  (`service.version`, `deployment.environment`, `host.name`,
  `process.pid` formatted as string, etc.) is a string. A typed
  variant can be added later without breaking the struct.
- **`service.name` stays a separate field.** It's the one Resource
  attribute every caller sets; giving it a dedicated slot is more
  ergonomic than requiring it in the array. The encoder emits it
  first, then the array entries in order.
- **Deep-copied at create.** The exporter owns its copy; the caller
  can free the input array immediately after
  `otlp_exporter_create()` returns. Matches how `service_name` and
  `user_agent` are handled.
- **Empty-key or empty-value entries skipped.** Matches the
  protobuf "empty fields omitted" convention used elsewhere in the
  library (avoids emitting degenerate KeyValue entries on the wire).

### Internal encoder changes (`src/otlp_messages.{h,c}`)

`otlp_emit_resource` extended:

```c
otlp_status_t otlp_emit_resource(struct otlp_pb_buf *parent,
                                  uint32_t field_num,
                                  const char *service_name,
                                  const otlp_resource_attr_t *attrs,
                                  size_t n_attrs);
```

The top-level encoders (`otlp_encode_export_trace_service_request`,
`otlp_encode_export_metrics_service_request`,
`otlp_encode_export_logs_service_request`) thread `(attrs, n_attrs)`
through to `otlp_emit_resource`. All three signals emit the same
Resource — this is DRY via the shared helper.

### Exporter wiring (`src/exporter.c`, `src/exporter_otel.c`)

- Exporter struct: new `otlp_resource_attr_t *resource_attributes`
  + `size_t n_resource_attributes` fields.
- `otlp_exporter_create`: deep-copies the array (allocates + dups
  each key + value via `otlp_dup_str`).
- `otlp_exporter_free`: frees each key + value, then the array.
- `otlp_exporter_otel_build_request` (traces path) and the two
  flush paths (metrics, logs) pass the stored attrs to their
  encoders.

### Test (`tests/property/test_property_resource_attrs.c`)

Four properties:

1. `prop_resource_empty` — no service, no attrs → 0 bytes.
2. `prop_resource_service_name_only` — service.name present.
3. `prop_resource_extra_attrs_encoded` — 3 extra attrs (version,
   environment, host) all present alongside service.name.
4. `prop_resource_attrs_skip_empty` — empty-key/empty-value
   entries omitted; service.name still present.

Uses the shared `walker.h` to descend
`ExportTraceServiceRequest → ResourceSpans → Resource` and scan the
repeated KeyValue list for each expected key/value pair. 28/28
tests pass under plain, TSAN, ASAN+UBSAN.

## Why this matters

This closes a real spec-compliance gap. Before v0.5.20, the library
was technically OTLP-compatible (service.name alone is valid) but
practically limited — most production deployments set additional
Resource attributes, and the library gave them no way to do so.

The design follows the project's invariants:

- **OCP**: existing public API unchanged; new functionality is a
  new field + new type. Adding typed values later is one new enum +
  one new struct variant, no break.
- **Model-driven**: the OTLP Resource schema is the source of
  truth; the encoder emits KeyValue pairs per the spec.
- **MECE**: the `otlp_emit_resource` helper owns Resource encoding
  for all three signals (traces, metrics, logs). No duplication.
- **DRY**: the deep-copy pattern in `otlp_exporter_create` mirrors
  the existing `service_name` + `user_agent` pattern.

## Acceptance criteria
- [x] Public type `otlp_resource_attr_t` declared in exporter.h.
- [x] Opts struct has `resource_attributes` + `n_resource_attributes`.
- [x] Exporter deep-copies at create, frees at free (ASAN-clean).
- [x] All three signal encoders emit the full Resource.
- [x] Property test verifies attrs appear on the wire.
- [x] 28/28 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
