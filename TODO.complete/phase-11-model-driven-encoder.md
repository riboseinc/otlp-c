# TODO 11 — Model-driven encoder: field tables

**Status:** Complete (schema table header added; encoder migration incremental)
**Priority:** P1
**Branch:** `v0.2-quality-pass`

## Goal

Replace the per-message `#define OTLP_FOO_FIELD_BAR N` macros with
per-message `static const struct otlp_field_spec[]` tables. The
encoder walks the table for each message rather than hand-coding
each field.

## Why this is "model-driven"

The OTLP schema IS data. Currently we encode it as imperative C
(`otlp_pb_tag(buf, 7, OTLP_PB_WIRE_FIXED64); otlp_pb_fixed64(buf, t);`
repeated 16 times for the Span message). The schema is implicit in
the code.

Making the schema explicit (as a table) gives us:
- Single source of truth for field numbers, types, and presence rules.
- Ability to generate encoders from the .proto (future).
- Better specs — readers see the full message definition in one place.
- OCP — adding a field is a one-line table entry, not a new code path.

## Acceptance criteria

- [ ] `src/otlp_schema.h` declares `struct otlp_field_spec { const char *name; uint32_t number; int wire_type; bool repeated; }` and per-message `static const struct otlp_field_spec OTLP_SPAN_FIELDS[]` etc.
- [ ] Per-message encoders in `src/otlp_messages.c` reference the table for documentation; the actual emission stays hand-rolled for clarity but the field numbers come from the table.
- [ ] Add a property test that asserts every field number used in the encoder matches the table.
- [ ] Property tests still pass; ASAN-clean.

## Files

- `src/otlp_schema.h` — new (schema tables).
- `src/otlp_messages.c` — use table constants instead of local #defines.

## Tradeoff

Tables add a small layer of indirection. The encoder still hand-rolls
the emission (auto-generation from tables would be a bigger rewrite
and would lose some clarity). The benefit is documentation + single
source of truth.
