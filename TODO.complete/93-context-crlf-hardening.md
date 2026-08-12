# TODO 93 — W3C context propagation CRLF hardening

**Status:** Complete (v0.5.53)
**Priority:** P0 (security: CWE-93 header injection via propagated context)

## What shipped

`otlp_context_extract` copied incoming tracestate and baggage
header values verbatim. If an attacker-controlled request
contained a value with `\r\n`, the value would propagate through
`otlp_context_inject` into the next outgoing request's carrier
callback. For HTTP-header-based carriers (the typical case), the
CRLF would split into a new header line — CWE-93 (HTTP request
splitting via propagated context).

The fix: reject values containing CR or LF at extract time.
Extract still succeeds (traceparent is preserved if valid);
tracestate/baggage are left empty when malformed.

## Sites changed

- `src/context.c::otlp_context_extract` — added `contains_crlf`
  check; skip tracestate/baggage copy if the value contains CR
  or LF.
- `tests/property/test_property_baggage.c` — added
  `prop_extract_rejects_crlf_tracestate` and
  `prop_extract_rejects_crlf_baggage`.

## Defense in depth

Combined with v0.5.52 (URL parser + user_agent validation), this
closes the third header-injection vector:

| Vector | Source | Fixed |
|---|---|---|
| URL parser | caller-supplied endpoint | v0.5.52 |
| `build_request` user_agent | caller-supplied user_agent | v0.5.52 |
| Context propagation | attacker-supplied incoming header | v0.5.53 |

The third vector is the most dangerous — it crosses trust
boundaries. A request from an attacker propagates header content
into a request to a trusted backend. The library now stops the
propagation at the trust boundary (extract).

## Why partial rejection is correct

If the carrier supplies a malicious tracestate, the right action
is to forward the legitimate traceparent without the malicious
tracestate. Trace correlation still works; only the vendor-
specific state is lost. W3C explicitly allows this: receivers
MAY truncate or drop non-conforming tracestate entries.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 48/48 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

## Audit context

Continues the audit pattern:
- v0.5.48-v0.5.49: OTLP wire format.
- v0.5.50: W3C LogRecord trace correlation.
- v0.5.51: memory safety + algorithm.
- v0.5.52: HTTP header injection (URL + user_agent).
- v0.5.53: HTTP header injection (context propagation).

The library is now hardened against all known header-injection
vectors in its outgoing HTTP requests.

## Next likely targets

- Traceparent / tracestate format edge cases (length limits,
  character validation).
- Tracer PRNG seeding (lower priority — not security-critical
  per OTLP spec).
- Span ID all-zero validation in setters.
- HTTP response parser: handling of chunked transfer encoding
  (currently the body would be misinterpreted — chunked
  responses are uncommon for OTLP but spec-legal).
