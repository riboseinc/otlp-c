# TODO 71 — emit_metric / emit_log (clone variants)

**Status:** Complete (v0.5.31)
**Priority:** P1 (API completeness)

## What shipped (v0.5.31)

### emit_metric + emit_log (clone variants)

Completes the emit API symmetry across all three signals.
v0.5.28 added move-semantics (`emit_metric_move` / `emit_log_move`).
v0.5.31 adds the clone counterparts:

| Signal | Clone (keep) | Move (give up) |
|---|---|---|
| Span | `emit()` | `emit_move()` |
| Metric | `emit_metric()` | `emit_metric_move()` |
| Log | `emit_log()` | `emit_log_move()` |

The clone variants deep-copy the metric/log before pushing into
the MPSC queue. The caller keeps ownership and may reuse or free
the original immediately. Use when the caller needs the original
after emit (e.g., emitting to multiple exporters).

### Internal clone functions

- `otlp_metric_clone(src)` — deep-copies ALL metric fields.
- `otlp_log_record_clone(src)` — deep-copies ALL log fields.

Both use `otlp_attribute_copy_all()` (new shared helper in
`internal_util.c`) for attribute deep-copying. DRY: the same
attribute-copy logic serves both clones and is available for
future span_clone refactoring.

### Design (MECE + OCP + DRY)

- **MECE**: each signal now has the same two-function emit
  surface (clone + move). No asymmetry.
- **OCP**: purely additive — new public functions, no existing
  API changed.
- **DRY**: `otlp_attribute_copy_all` extracted into shared
  internal_util.c, used by both clone functions. The string/
  bytes attribute cases handle owned-memory duplication; the
  int64/double/bool cases are direct copies.

## Acceptance criteria
- [x] `otlp_exporter_emit_metric` public function.
- [x] `otlp_exporter_emit_log` public function.
- [x] `otlp_metric_clone` internal function.
- [x] `otlp_log_record_clone` internal function.
- [x] `otlp_attribute_copy_all` shared helper.
- [x] Property test: emit_metric (clone) keeps original usable.
- [x] 33/33 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
