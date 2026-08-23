# TODO 147 — Documentation freshness sweep (first 0.6.x release)

**Status:** Complete (v0.6.1)
**Priority:** P3 (docs; the README is the front door)

## What shipped

**README feature list** — three staleness bugs and two gaps,
all real drift from shipped capabilities:

- Resource attributes were described as "(string/int64/double/
  bool)" — the pre-v0.5.92 model; now the full `otlp_value_t`
  value model with create-time map semantics.
- Diagnostics described only `set_logger`; now leads with the
  structured `set_event_logger` surface (v0.5.100) — "two views
  of one model".
- Added: server-response awareness (Retry-After honored,
  PartialSuccess surfaced via diagnostics + `rejected_*` stats —
  v0.5.95/96) and UTF-8 boundary validation (v0.5.103).
- The single example link became the full list: minimal,
  multithread, and the new event_loop_integration.

**cookbook.md** — the event-loop section now points at the
runnable `examples/event_loop_integration.c` (v0.5.105) next to
the libuv adaptation prose.

**Process note**: a suspected copy-paste duplication in
quickstart.md's minimal.c snippet turned out to be an artifact
of two concatenated `sed` outputs in one tool result — the
assertion-guarded replace refused the false anchor and the file
was left untouched. Verified clean by re-reading.

**Scope discipline**: documentation-only diff — appropriate for
the first release inside the 0.6 additive-only stabilization
window (no code, no API).
