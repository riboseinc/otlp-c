# TODO 22 — Span events and links

**Status:** Complete (v0.5)
**Priority:** P1
**Depends on:** nothing

## Goal

Implement Span.Event (field 11) and Span.Link (field 13) end-to-end:
API, storage, encoder, clone.

## What shipped (v0.5)

**Public API** (`include/otlp-c/span.h`):
- `otlp_span_add_event(span, name, time_unix_nano)` — appends an Event
  with name + timestamp.
- `otlp_span_add_link(span, trace_id, span_id)` — appends a Link with
  the referenced trace/span pair.
- `otlp_span_set_trace_state(span, trace_state)` — sets the W3C
  tracestate header (field 3 of Span).

**Internal layout** (`src/span_internal.h`):
- `struct otlp_event` — name + time.
- `struct otlp_link` — trace_id[16] + span_id[8].
- `struct otlp_span` extended with `events[64]`, `links[64]`,
  `trace_state` (owned string, may be NULL).

**Storage** (`src/span.c`):
- Fixed-cap arrays: 64 events, 64 links per span. Overflow returns
  `OTLP_ERR_OVERFLOW`. Same pattern as the 128-attribute cap.
- `otlp_span_free` releases event names, trace_state.
- `otlp_span_clone` deep-copies events, links, trace_state.

**Schema** (`src/otlp_schema.h`):
- New `OTLP_EVENT_FIELDS[]` table: name=1, time=2, attributes=3,
  dropped_attributes_count=4.
- New `OTLP_LINK_FIELDS[]` table: trace_id=1, span_id=2,
  trace_state=3, attributes=4, dropped_attributes_count=5, flags=6.

**Encoder** (`src/otlp_messages.c`):
- Emits trace_state at field 3 when non-empty.
- Emits events at field 11 as repeated sub-messages, each containing
  name{1} + time{2}.
- Emits links at field 13 as repeated sub-messages, each containing
  trace_id{1} + span_id{2}.
- Field numbers come from `OTLP_SPAN_FIELDS[OTLP_SPAN_FI_*]` — single
  source of truth.

**Property tests** (`tests/property/test_property_events_context.c`):
- `prop_events_field_roundtrip` — event name + time round-trip (200 iters).
- `prop_links_field_roundtrip` — link trace_id + span_id round-trip (50 iters).
- `prop_trace_state_field` — trace_state field 3 emitted (200 iters).
- `prop_span_clone_copies_extras` — clone preserves events/links/state (50 iters).

## Acceptance criteria
- [x] CI green on macOS arm64 (build verified locally).
- [x] No regression in existing tests.
- [x] Property tests pass deterministically.

## Out of scope (deferred)
- Event attributes (API extension).
- Link attributes, link trace_state, link flags (API extension).
- `dropped_attributes_count` fields.
