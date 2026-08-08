# TODO 62 — W3C Baggage + traceparent_format_raw DRY

**Status:** Complete (v0.5.22)
**Priority:** P1

## What shipped (v0.5.22)

### 1. W3C Baggage propagation

The library propagates the W3C
[Baggage](https://www.w3.org/TR/baggage/) header alongside
traceparent and tracestate. Baggage carries arbitrary key-value
pairs (user IDs, request IDs, feature flags, canary flags) across
service boundaries.

**Why this matters:** distributed tracing without baggage is
incomplete. A trace shows you the call graph, but baggage shows
you the request-scoped data that DROVE those calls. Real OTel
deployments use both. The library already did traceparent +
tracestate (W3C Trace Context); this completes the W3C
propagation story.

**Public API** (additive — OCP compliant):

```c
#define OTLP_CONTEXT_BAGGAGE_MAX 2048

typedef struct otlp_context {
    /* ... existing fields ... */
    char baggage[OTLP_CONTEXT_BAGGAGE_MAX];  /* NEW */
} otlp_context_t;

extern const char OTLP_CONTEXT_BAGGAGE_HEADER[];  /* "baggage" */
```

**Design decisions:**

- **Opaque string, not parsed map.** Matches the `tracestate`
  contract: the library propagates without interpreting.
  Callers format on inject, parse on extract. Keeps the library
  simple and avoids defining a baggage-entry data type that
  would need its own lifecycle, encoding/decoding, percent-
  handling, property syntax, etc.
- **2048-byte cap.** W3C recommends at least 8192; we document
  the lower cap and note callers with larger baggage should use
  a side-channel. Keeps `otlp_context_t` (now ~2600 bytes) from
  bloating further.
- **`inject`/`extract` updated to handle baggage automatically.**
  A caller that fills `ctx.baggage` gets it propagated for free;
  a caller that doesn't sees identical behavior to before (no
  header written, no field populated). This is the ergonomic
  choice — propagation should be all-or-nothing per context.

### 2. `otlp_traceparent_format_raw` primitive (DRY)

The traceparent hex-formatting logic was duplicated:

- `src/w3c.c::otlp_traceparent_format` — takes a span, extracts
  IDs, formats.
- `src/context.c::otlp_context_inject` — has raw IDs from the
  context struct, inlined its own ~20-line copy of the hex
  formatting with a comment explaining why it couldn't call
  `otlp_traceparent_format` (which takes a span, not raw bytes).

**Fix:** extracted the raw-bytes version as a new public
primitive:

```c
otlp_status_t otlp_traceparent_format_raw(
    const uint8_t trace_id[16],
    const uint8_t span_id[8],
    bool sampled,
    char *buf, size_t cap,
    size_t *out_len);  /* optional; may be NULL */
```

- `otlp_traceparent_format` (span-based) now extracts IDs from
  the span and delegates to `format_raw`.
- `otlp_context_inject` calls `format_raw` directly, removing
  the duplicated hex formatting.
- `out_len` is now optional (NULL means "don't care"). The
  span-based wrapper always passed a valid pointer; the context
  inject case doesn't need the length. Making it optional
  aligns the API for both callers.

**Why both DRY and API completion:** the raw-bytes version is
the fundamental operation (format bytes → string); the span-
based version is a convenience wrapper. Exposing the primitive
lets callers who have raw IDs (from a context, from a custom
ID generator, from a parsed traceparent) format without needing
a span pointer.

### 3. Property tests

`tests/property/test_property_baggage.c` (5 properties):

- `prop_baggage_roundtrip` — 50 iterations. Inject with random
  IDs + a known baggage string, extract, assert baggage matches.
- `prop_baggage_absent_on_extract` — 20 iterations. Carrier
  without baggage header → extracted baggage is empty string.
- `prop_baggage_with_tracestate` — 30 iterations. Both headers
  coexist; both round-trip correctly.
- `prop_baggage_header_constant` — 1 iteration. The header name
  constant is the string `"baggage"`.
- `prop_format_raw_matches_format` — 50 iterations. DRY
  regression check: `format_raw` and `format` produce identical
  bytes for the same span.

## Verification

```
ctest --test-dir build          # 30/30 pass
ctest --test-dir build-tsan     # 30/30 pass, zero race reports
ctest --test-dir build-asan     # 30/30 pass (ASAN + UBSAN)
```

Zero compiler warnings across all three configurations.

## Why this matters (architecture view)

The context module now implements the COMPLETE W3C propagation
surface:

| Header | Spec | Field on context | Handled by |
|---|---|---|---|
| `traceparent` | W3C Trace Context | `trace_id`, `span_id`, `sampled` | inject/extract |
| `tracestate` | W3C Trace Context | `tracestate[512]` | inject/extract |
| `baggage` | W3C Baggage | `baggage[2048]` | inject/extract |

All three are opaque to the library (the caller formats/parses).
All three are optional on extract. All three are conditionally
written on inject (only when non-empty). This is MECE: each
header is a distinct propagation concern handled in exactly one
place.

The DRY extraction completes the w3c.h API: callers can now
format a traceparent from either a span (`otlp_traceparent_format`)
or raw bytes (`otlp_traceparent_format_raw`). Both paths share
one implementation.

## Acceptance criteria
- [x] `OTLP_CONTEXT_BAGGAGE_MAX` and `baggage` field on context.
- [x] `OTLP_CONTEXT_BAGGAGE_HEADER` constant.
- [x] `inject` writes baggage header when non-empty.
- [x] `extract` reads baggage header when present.
- [x] `otlp_traceparent_format_raw` public primitive.
- [x] `otlp_traceparent_format` delegates to `format_raw`.
- [x] `otlp_context_inject` uses `format_raw` (no duplicated hex).
- [x] `out_len` optional (NULL-safe) on both format functions.
- [x] 30/30 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
