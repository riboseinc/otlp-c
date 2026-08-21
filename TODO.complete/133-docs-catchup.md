# TODO 133 — Documentation catch-up for v0.5.87–v0.5.92

**Status:** Complete (v0.5.93)
**Priority:** P2 (docs accuracy; one example was outright broken)

## What shipped

- **`docs/cookbook.md`**: the resource-attributes example still
  used the parallel-fields struct deleted in v0.5.92 — broken
  since that release shipped. Rewritten to `{key, otlp_value_t}`
  with the four classic examples (string/int64/double/bool).
- **`docs/roadmap.md`**: version rows for v0.5.87–v0.5.92 (PR
  numbers from merge history), key-metrics block refreshed
  (132 TODOs, 39 tests, 43+ bugs, 82%+ coverage everywhere), and
  a unification-arc summary paragraph (one value type, one
  storage model, one set engine, one encoder dispatch — across
  all six attribute surfaces).
- **`CLAUDE.md`**: resource attributes documented as the sixth
  attribute surface; "five surfaces" → six in the conventions and
  implementing-agent sections.

No code changes; build + full suite as the gate.

## Lesson

A breaking API change isn't done when the code compiles — the
docs examples that use the old API break silently. The cookbook
example had been wrong for one full release before this audit.
