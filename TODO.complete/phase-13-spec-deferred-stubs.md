# TODO 13 — Spec completion: deferred-feature stubs

**Status:** Complete
**Priority:** P1
**Branch:** `v0.2-quality-pass`

## Goal

Document OTLP features that are intentionally deferred (events,
links, trace_state, ArrayValue, KeyValueList) and add stub functions
that return `OTLP_ERR_NOT_IMPLEMENTED`. This makes the API surface
explicit about what's missing and gives callers a migration path.

## Acceptance criteria

- [ ] `include/otlp-c/span.h` adds:
  - `otlp_span_add_event(span, name, time_unix_nano)` → NOT_IMPLEMENTED
  - `otlp_span_add_link(span, trace_id, span_id)` → NOT_IMPLEMENTED
  - `otlp_span_set_trace_state(span, tracestate)` → NOT_IMPLEMENTED
- [ ] All three documented as "deferred to v0.2+" with the OTLP spec section reference.
- [ ] `docs/otlp-spec.md` updated with a "Deferred features" section listing each + the version target.
- [ ] CHANGELOG entry under v0.2 noting the stubs.
- [ ] Property test asserts the stubs return `OTLP_ERR_NOT_IMPLEMENTED`.

## Files

- `include/otlp-c/span.h` — add 3 declarations.
- `src/span.c` — add 3 stubs.
- `docs/otlp-spec.md` — add deferred-features section.
- `CHANGELOG.md` — note the stubs.
- `tests/property/test_property_span.c` — assert stubs.

## Why

Silent omissions are worse than explicit stubs. A caller reading the
header sees the full OTLP span surface and knows which fields are
deferred. The stubs return a clear status code rather than producing
a malformed wire payload.

## Tradeoff

Three stubs is mild API bloat. They'll be replaced with real
implementations in v0.2.x. Documented as part of the API contract.
