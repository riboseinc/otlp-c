# TODO 23 — W3C Trace Context propagation

**Status:** Complete (v0.5)
**Priority:** P1
**Depends on:** nothing

## Goal

Provide a transport-agnostic context propagation API. Caller can
serialize the trace context into any carrier (HTTP header map,
gRPC metadata, message attribute table) and deserialize on the
other side.

## What shipped (v0.5)

**Public API** (`include/otlp-c/context.h`):
- `otlp_context_t` — value-type struct: trace_id[16], span_id[8],
  has_context flag, sampled flag.
- `otlp_carrier_set_fn` / `otlp_carrier_get_fn` — callback types
  for the carrier abstraction. The caller provides these so the
  library can read/write any transport.
- `OTLP_CONTEXT_TRACEPARENT_HEADER` / `OTLP_CONTEXT_TRACESTATE_HEADER`
  — exported header name constants.
- `otlp_context_from_span(span)` — snapshot a span's identity.
- `otlp_context_inject(ctx, set, carrier)` — serialize via callback.
- `otlp_context_extract(get, carrier)` — deserialize via callback.

**Implementation** (`src/context.c`):
- Inject formats the traceparent header (same format as w3c.c) and
  calls `set(carrier, "traceparent", value)`.
- Extract calls `get(carrier, "traceparent")` and parses via
  `otlp_traceparent_parse`. Returns `has_context=false` on missing
  or invalid header.
- The carrier is opaque to the library — `void *carrier_ctx`.
- No transport-specific code; no HTTP/gRPC coupling.

**Property tests** (`tests/property/test_property_events_context.c`):
- `prop_context_inject_extract` — round-trip via in-memory carrier
  (50 iters, PRNG-seeded).
- `prop_context_extract_rejects_malformed` — empty carrier, garbage
  header, and all-zero trace_id all yield `has_context=false`.

## Design notes

The callback-based carrier is a deliberate choice. The alternative
— a built-in `otlp_header_map` type — would force the library to
own a string map, which couples it to a specific transport and
adds memory management surface. The callback pattern lets the
caller pass a curl `curl_slist`, a libuv HTTP header struct, a
gRPC `grpc_metadata` array, or anything else, with no library
changes. OCP: a new transport requires no library modification.

## Acceptance criteria
- [x] CI green on macOS arm64.
- [x] No regression in existing tests.
- [x] Property tests pass deterministically.

## Out of scope (deferred)
- tracestate header injection/extraction (currently inject emits
  traceparent only; extract ignores tracestate). Tracked as a
  follow-up — requires storing a list of vendor entries in
  `otlp_context_t`.
- Baggage propagation (W3C Baggage).
- Thread-local "current span" / request-scoped context storage.
  The library is explicit-context: callers thread the context
  through their own code.
