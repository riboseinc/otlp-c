# TODO 121 — W3C context-propagation audit

**Status:** Complete (v0.5.81)
**Priority:** P1 (spec compliance + hardening on an untrusted-input parser)

## What shipped

Audit of the context-propagation surface (traceparent parse /
format, tracestate and baggage pass-through) against the W3C Trace
Context and Baggage specs. The parser is memory-safe by
construction (each read only happens after the previous character
passed validation, so reads never cross the NUL terminator —
verified by tracing every path). Findings:

**1. Version rules (W3C §3.3.2) were missing entirely.**
- Version `ff` was accepted — the spec says 0xff is invalid
  outright.
- Version 00 with trailing content (`…-01-junk`) was accepted —
  the spec says version 00 is exactly 4 fields; trailing content
  makes the header invalid.
- Future versions' extra fields must be ignored (forward
  compatibility) — the parser already did this; now it's the
  *documented, tested* rule rather than an accident of not
  looking past the flags.

The fix reads `header[55]` — in bounds because the flags chars at
[53]/[54] already validated, so [55] is at worst the NUL.

**2. Extract-side tracestate/baggage only rejected CR/LF.** Other
control bytes (0x00–0x1F except none, 0x7F) passed through and
would produce invalid outgoing headers on inject — the same
CWE-93 family one step removed. `contains_crlf` became
`contains_control` (rejects any byte < 0x20 or 0x7F); W3C
tracestate/baggage grammars only allow printable ASCII.

**Not changed (audited, found correct):**
- Hex parsing is case-insensitive; format emits lowercase (spec
  SHOULD).
- The formatter always emits version 00 — correct for a producer.
- All-zero trace-id/span-id rejection (v0.5.54) and CRLF
  rejection (v0.5.53) hold.
- No over-read on short/malformed headers (see above).

## Tests

- `prop_traceparent_version_rules` — ff rejected; version 00 +
  trailing junk rejected; version 01 + future field accepted with
  correct IDs/flags; uppercase hex accepted.
- `prop_context_rejects_control_bytes` — a carrier tracestate
  containing 0x01 is dropped on extract while the context itself
  still extracts. (First draft of this test was vacuous — the
  span had no tracestate so nothing was corrupted; rewritten to
  inject a real tracestate entry first, then taint it.)

## Sites changed

- `src/w3c.c` — version rules.
- `src/context.c` — `contains_control`.
- `tests/property/test_property_w3c.c`,
  `tests/property/test_property_events_context.c` — properties.

## Verification

```
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout          # 37/37
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -E "http-timeout|url-parse"
```
