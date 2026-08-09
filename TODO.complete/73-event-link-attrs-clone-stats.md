# TODO 73 — Fix span_clone event/link attr loss + flush stats gap

**Status:** Complete (v0.5.33)
**Priority:** P0 (data-loss bug fix)

## What shipped (v0.5.33)

### 1. CRITICAL FIX: span_clone dropped event/link attributes

`otlp_span_clone` (used by every `emit()` call) copied events
with only `name` + `time_unix_nano`, and links with only
`trace_id` + `span_id`. The event/link **attributes were silently
dropped**.

Event structs have `attrs[OTLP_EVENT_MAX_ATTRS]` and link structs
have `attrs[OTLP_LINK_MAX_ATTRS]`. The public API
(`otlp_span_set_event_attribute_string`, `otlp_span_set_link_attribute_string`)
sets these attributes. But the clone function never copied them.

Impact: any span emitted via `emit()` that had event/link
attributes sent INCOMPLETE DATA to the collector. The attributes
existed on the caller's original span but were lost in the
deep-copy.

**Why not caught earlier:** the existing `prop_span_clone_copies_extras`
test created events with only name + time (no attributes). The
test verified clone correctness for the fields it set, but didn't
exercise the attribute-copy path.

Fix: clone now calls `otlp_attribute_copy_all` for each event's
and link's attribute array after adding the event/link to the
clone.

New regression test: `prop_span_clone_preserves_evlink_attrs` —
adds event + link attributes, clones, verifies preservation.

### 2. Fixed: flush_metric / flush_log stats gap

The synchronous flush functions didn't update per-signal stats
(`emitted_metrics`, `sent_metrics`, `dropped_metrics_err`,
`emitted_logs`, `sent_logs`, `dropped_logs_err`). Only the async
pipeline updated these.

Fix: both functions now increment the appropriate counters.

## Acceptance criteria
- [x] span_clone copies event attributes via otlp_attribute_copy_all.
- [x] span_clone copies link attributes via otlp_attribute_copy_all.
- [x] Regression test: prop_span_clone_preserves_evlink_attrs.
- [x] flush_metric updates emitted_metrics + sent/dropped.
- [x] flush_log updates emitted_logs + sent/dropped.
- [x] 33/33 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
