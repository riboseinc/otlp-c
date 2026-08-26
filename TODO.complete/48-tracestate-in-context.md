# TODO 48 — tracestate header in SpanContext propagation

**Status:** Complete (v0.5.4)
**Priority:** P2
**Depends on:** nothing

## Goal

Extend `otlp_context_inject` / `_extract` to also propagate the
W3C tracestate header (currently only traceparent is handled).

## Background

The W3C Trace Context spec defines two headers:
- `traceparent`: the trace-id + span-id + sampled flag
- `tracestate`: vendor-specific list of `key=value` entries

`otlp-c` v0.5.0 added context injection but only the traceparent
header. The tracestate is deferred because:
- It's a multi-entry list, not a single value — needs storage on
  `otlp_context_t`.
- Vendor-specific semantics — the library doesn't interpret them.

## Design

**Public API** (`include/otlp-c/context.h`):
```c
typedef struct otlp_tracestate_entry {
    char *key;    /* owned */
    char *value;  /* owned */
} otlp_tracestate_entry_t;

struct otlp_context {
    uint8_t trace_id[OTLP_TRACE_ID_LEN];
    uint8_t span_id[OTLP_SPAN_ID_LEN];
    bool has_context;
    bool sampled;
    /* New: tracestate entries (up to 32 per W3C spec). */
    otlp_tracestate_entry_t tracestate[32];
    size_t n_tracestate;
};

/* Inject both traceparent and tracestate. */
otlp_status_t otlp_context_inject(otlp_context_t ctx,
                                  otlp_carrier_set_fn set,
                                  void *carrier_ctx);

/* Extract both headers; tracestate parsed into the entries array. */
otlp_context_t otlp_context_extract(otlp_carrier_get_fn get,
                                    void *carrier_ctx);
```

**Wire format** for tracestate (per W3C):
- Single header value: `key1=value1,key2=value2,...`
- Multiple header lines allowed (concatenate with comma)
- Max 32 entries; oldest dropped first

**Implementation** (`src/context.c`):
- `inject`: format tracestate from entries array →
  `set(carrier, "tracestate", value)`
- `extract`: read `get(carrier, "tracestate")` → parse comma-separated
  `key=value` pairs into entries

## Acceptance criteria
- [x] `otlp_context_t` carries up to 32 tracestate entries.
- [x] Inject emits both traceparent and tracestate.
- [x] Extract parses tracestate into entries; missing header = 0 entries.
- [x] Property tests for round-trip + max-entries cap.
- [x] Backwards compat: existing context_from_span / inject / extract
      still work for spans without tracestate.

## Out of scope (deferred further)
- Tracestate parsing strict mode (reject malformed entries).
- Per-vendor tracestate validation.
