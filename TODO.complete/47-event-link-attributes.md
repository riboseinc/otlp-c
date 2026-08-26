# TODO 47 — Event + Link attributes API

**Status:** Complete (v0.5.4)
**Priority:** P2
**Depends on:** nothing

## Goal

Extend the Span events/links API to accept attributes. Currently:

```c
otlp_span_add_event(span, name, time);          /* name + time only */
otlp_span_add_link(span, trace_id, span_id);    /* ids only */
```

The OTLP Event and Link messages support attributes (field 3 for
Event, field 4 for Link), but the library's API doesn't expose them.

## Background

The v0.5 implementation of events/links deliberately omitted
attributes to keep the storage simple (fixed-cap inline arrays).
Once events/links are common, attributes become important —
event payloads, link metadata, etc.

## Design

**Public API** (`include/otlp-c/span.h`):
```c
/* Existing — keep for backwards compat. */
otlp_status_t otlp_span_add_event(otlp_span_t *span,
                                  const char *name,
                                  uint64_t time_unix_nano);

/* New — builder pattern for events with attributes. */
typedef struct otlp_event_builder otlp_event_builder_t;
otlp_event_builder_t *otlp_event_builder_create(const char *name,
                                                uint64_t time_unix_nano);
otlp_status_t otlp_event_builder_set_attribute_string(
    otlp_event_builder_t *b, const char *key, const char *val);
/* ... int, double, bool, bytes ... */
otlp_status_t otlp_span_add_event_built(otlp_span_t *span,
                                        otlp_event_builder_t *b);
void otlp_event_builder_free(otlp_event_builder_t *b);

/* Same pattern for links. */
typedef struct otlp_link_builder otlp_link_builder_t;
otlp_link_builder_t *otlp_link_builder_create(const uint8_t *trace_id,
                                              const uint8_t *span_id);
/* setters ... */
otlp_status_t otlp_span_add_link_built(otlp_span_t *span,
                                       otlp_link_builder_t *b);
void otlp_link_builder_free(otlp_link_builder_t *b);
```

The builder pattern keeps the existing simple API working while
allowing rich event/link construction.

**Internal storage** (`src/span_internal.h`):
```c
struct otlp_event {
    char *name;
    uint64_t time_unix_nano;
    struct otlp_attribute attrs[OTLP_EVENT_MAX_ATTRS];  /* new */
    size_t n_attrs;                                      /* new */
};

struct otlp_link {
    uint8_t trace_id[OTLP_TRACE_ID_LEN];
    uint8_t span_id[OTLP_SPAN_ID_LEN];
    char *trace_state;                                   /* new */
    struct otlp_attribute attrs[OTLP_LINK_MAX_ATTRS];    /* new */
    size_t n_attrs;                                      /* new */
    uint32_t flags;                                      /* new */
};
```

Caps: `OTLP_EVENT_MAX_ATTRS = 32`, `OTLP_LINK_MAX_ATTRS = 32`.

**Encoder** (`src/otlp_messages.c`):
The traces encoder already emits Event attributes via
`otlp_emit_attributes` (the schema table for Event has field 3 =
attributes). Just need to wire `otlp_span_get_event_attrs` accessor
and call `otlp_emit_attributes` in `emit_event`.

## Acceptance criteria
- [x] Builder API for events + links.
- [x] Storage for attributes on event/link structs.
- [x] Encoder emits Event.attributes and Link.attributes.
- [x] Link.trace_state and Link.flags emitted.
- [x] Property tests for round-trip.
- [x] Backwards compat: existing `otlp_span_add_event` /
      `otlp_span_add_link` continue to work (zero attributes).

## Out of scope (deferred further)
- Dropped attributes count fields.
- More than 32 attributes per event/link (caller can monitor via
  the new builder returning OTLP_ERR_OVERFLOW).
