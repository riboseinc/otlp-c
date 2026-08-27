# ADR 0006 — Module boundaries hold: no further splits

## Status

Accepted, 2026-08-27, after the fourth architecture review
(at v1.1.7). Records the review's decline of all remaining
extraction candidates so future reviews do not re-litigate
them.

## Context

Three architecture reviews shipped deepening work: the HTTP
response parser and retry timing as pure modules (R1), one
signal table and the sync-flush unification (R2), the
exporter_sync.c extraction with exporter_internal.h as the one
internal seam (R3). Review #4 (2026-08-27) walked the hot
spots — `exporter.c` (1471 lines), `otlp_messages.c` (788),
`internal_util.c` (880), the encoder layering, and the
internal include graph — and applied the deletion test to every
remaining extraction candidate.

## Decision

The module map is at its intended depth. We decline:

1. **Diagnostics extraction** (`format_event`, event
   accounting, `report_partial_success` →
   `exporter_events.c`). The depth is the invariant — one
   event model, one derived formatter (v0.5.100) — not the
   filename. Extraction adds a new internal seam to relocate
   ~180 lines; the coverage floor already forces every path.
2. **otlp_messages.c split** (shared AnyValue/KV/resource/scope
   emission vs the traces encoder). The boundary already exists
   as `otlp_messages.h`: the metrics and logs encoders consume
   exactly `otlp_emit_resource` / `_instrumentation_scope` /
   `_attributes` / `otlp_encode_any_value`. Splitting renames
   a working seam.
3. **Pure outcome classifier** for tick()'s HTTP result
   handling. The retry half is already pure
   (`retry_policy.c`, property-tested); the remaining glue has
   an interface as wide as its body.
4. **internal_util.c split** — declined in R1, R2, and R3. The
   one-set-attribute-engine locality IS the design.

Evidence the shape is finished: all three signal encoders
layer on exactly `otlp_messages.h` + `otlp_schema.h` +
`protobuf_encode.h`; the internal include graph has no
cross-seam reaches; every file clears the 82% coverage floor;
CONTEXT.md and the architecture MECE table match the code.

## Consequences

- (+) Reviews stop paying the churn tax of moving cohesive
  code between files.
- (+) The next module-shape change is feature-driven, not
  taste-driven — a 4th signal (profiles) lands as one
  `SIGNAL_SPECS[]` row plus one encoder, per the table's
  design.
- (−) Large files stay large (`exporter.c` 1471 lines). If a
  single function grows past ~300 lines or acquires a second
  reason to change, that function — not the module — is the
  extraction unit, and this ADR does not block it.

## When this changes

A 4th signal, the 2.x optional-deps line, or any change that
gives a declined candidate a second consumer (making the seam
real rather than hypothetical) reopens this decision.
