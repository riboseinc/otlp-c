# TODO 64 — Typed Resource attributes

**Status:** Complete (v0.5.24)
**Priority:** P1 (spec compliance, completes TODO 60)

## What shipped (v0.5.24)

### Typed Resource attribute values

The OTLP semantic conventions define Resource attributes as
int (`process.pid`, `host.cpu.count`), double
(`system.memory.utilization`), and bool (`cloud.auto_scale`)
in addition to the common string case. Until v0.5.24, the
library's `otlp_resource_attr_t` was string-only (shipped in
v0.5.20, TODO 60). This completes the feature.

**Design: source-level backward compatibility.**

The struct grows new fields AFTER the existing `key` + `value`,
so existing designated-initializer callers (`{.key = "k",
.value = "v"}`) need no changes. `.type` defaults to 0
(STRING) when zero-initialized; `.value` is used as before.

```c
typedef enum {
    OTLP_RESOURCE_ATTR_STRING = 0,  /* default */
    OTLP_RESOURCE_ATTR_INT64  = 1,
    OTLP_RESOURCE_ATTR_DOUBLE = 2,
    OTLP_RESOURCE_ATTR_BOOL   = 3,
} otlp_resource_attr_type_t;

typedef struct {
    const char *key;
    const char *value;                  /* STRING path */
    otlp_resource_attr_type_t type;     /* 0 = STRING */
    int64_t int64_val;                  /* INT64 path */
    double  double_val;                 /* DOUBLE path */
    bool    bool_val;                   /* BOOL path */
} otlp_resource_attr_t;
```

This is NOT binary-compatible (struct grew). Within the 0.x
line, source-level API breaks are allowed per CLAUDE.md. The
CHANGELOG entry documents the field additions.

**Encoder dispatch: table-driven (OCP).**

`otlp_emit_resource` maps the public type enum to the internal
`otlp_attr_type`, then the existing `attr_encoders[]` table
(from v0.5.7) handles the wire encoding. A switch in the
resource encoder maps 4 public types to 4 internal types; the
internal dispatch table does the actual encoding. Adding a new
value type (e.g., bytes) is one enum entry + one table row +
one switch case — the switch is the fixed mapping point, the
table is the extension point.

**Exporter deep-copy.**

`otlp_exporter_create` copies `type` + all value fields.
`.value` is always duplicated for STRING attrs; for other
types it may be NULL. The free path calls `otlp_free()` on
both `.key` and `.value` unconditionally — `otlp_free(NULL)`
is a no-op (via libc `free`), so this is safe regardless of
type.

### Tests

`tests/property/test_property_resource_attrs.c` extended from
4 to 7 properties:

- `prop_resource_typed_int64` — `process.pid = 4242` (INT64)
  appears on the wire; `service.name` still present (backward
  compat).
- `prop_resource_typed_bool` — `cloud.auto_scale = true` (BOOL)
  appears on the wire.
- `prop_resource_mixed_types` — string + int64 + bool + double
  all coexist in one Resource; string value still round-trips.

Uses a shared `find_key` helper that walks the wire to verify a
given key is present at the Resource level. Exact value-byte
encoding is covered by the existing AnyValue encoder tests
(`test_property_attribute_roundtrip.c`); the resource test
verifies the encoder dispatches types correctly without
duplicating the per-type wire walk.

### Bug fix: existing tests had uninitialized fields

The v0.5.20 resource-attr tests declared
`otlp_resource_attr_t attrs[3];` without initialization. Before
v0.5.24 the struct had only `key` + `value` (both explicitly
set), so uninitialized fields didn't matter. After v0.5.24 the
struct has `type` + value fields — uninitialized `.type` could
be garbage, breaking the STRING dispatch. Fixed with `memset`
in each affected test.

## Why this matters

This closes the last spec-compliance gap in the Resource
message. The OTLP semantic conventions spec defines ~60
standard Resource attributes; ~50 are strings, ~8 are ints,
~2 are doubles/bools. v0.5.20 covered the 50 strings; v0.5.24
covers the remaining 10.

The design follows the project's invariants:
- **OCP**: existing callers unchanged; new functionality is
  new fields. The encoder dispatch table is the extension
  point for future value types.
- **MECE**: `otlp_resource_attr_t` is the single type for all
  Resource attributes (was already; now covers all value
  variants). The encoder has one mapping point (public →
  internal type enum) and one dispatch point (the
  `attr_encoders[]` table).
- **Model-driven**: the type enum matches the OTLP AnyValue
  oneof variants exactly. The schema (`otlp_schema.h`)
  defines the field numbers; the encoder reads them from
  the schema table, not hardcoded.
- **DRY**: the resource encoder delegates to the shared
  `otlp_encode_key_value` helper, which delegates to the
  shared `attr_encoders[]` table. No per-type encoding code
  in the resource path.

## Acceptance criteria
- [x] `otlp_resource_attr_type_t` enum with STRING/INT64/DOUBLE/BOOL.
- [x] `otlp_resource_attr_t` struct extended with type + value fields.
- [x] Existing `{.key, .value}` initializers work unchanged.
- [x] Encoder dispatches all 4 types via the internal attr table.
- [x] Exporter deep-copies all value fields.
- [x] 31/31 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
- [x] 3 new typed-value properties (int64, bool, mixed).
