# TODO 117 — Documentation catch-up for the attribute-model arc

**Status:** Complete (v0.5.77)
**Priority:** P2 (accuracy: docs lagged 9 releases of API surface)

## What shipped

The v0.5.68–v0.5.76 arc shipped major API surface and behavior —
the public `otlp_value_t` composite-attribute type, ten
array/kvlist setters, map (upsert) semantics, and the
grow-on-demand storage model — with no docs updates. Every stale
claim found and fixed:

- `docs/roadmap.md` — version table stopped at v0.5.63 with
  "Key metrics (v0.5.63)". Added rows for v0.5.64–v0.5.76 (PR
  numbers verified against `gh pr list`, not guessed), refreshed
  the key-metrics block (116 TODOs, 36 tests, 37+ bugs, span
  struct 176 B, ~150 ns/span emit), and added an
  attribute-model-arc summary paragraph.
- `docs/otlp-spec.md` — the AnyValue section claimed "v0.1.0
  supports string/bool/int64/double/bytes; ArrayValue and
  KeyValueList are tracked as P2." Now documents the full oneof
  including composite setters, the `otlp_value_t` input model,
  the no-nesting caveat, and the map/uniqueness semantics.
- `README.md` — feature list gained the attribute bullet: full
  AnyValue set on all five surfaces, map semantics,
  grow-on-demand storage.
- `CLAUDE.md` — key-files table gained `value.h`; Conventions
  gained the "Attributes are a map" entry (one shared vector
  model, `otlp_attr_vec_*` ownership, build-then-attach contract);
  the implementing-agent notes now state that all five surfaces
  take all seven types.
- `docs/cookbook.md` — new section 16: composite attributes
  (array + kvlist code example, upsert note). The old flush-timeout
  section renumbered 16 → 17.

`docs/quickstart.md` audited — current as-is (uses only string
setters, no stale claims).

Docs-only release: no code, no tests, no behavior change; the
build and suite run as the regression gate.

## Verification

```
cmake --build build && ctest --test-dir build -E http-timeout   # 36/36
```
