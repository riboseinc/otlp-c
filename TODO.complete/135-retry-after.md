# TODO 135 — Honor Retry-After on throttled responses

**Status:** Complete (v0.5.95)
**Priority:** P2 (protocol-conformance gap)

## The gap

The retry pipeline (v0.5.83) already did full-jitter exponential
backoff on 429/503/5xx — but it ignored the server's Retry-After
header (RFC 7231 §7.1.3). A throttling collector that says "retry
in 5s" got hammered at our jittered pace (potentially ~0ms on the
first attempt), making throttling worse. Spec-conformant clients
honor the server's floor.

## What shipped

**HTTP client** (`src/http_client.{h,c}`):
- Line-aligned `Retry-After:` parsing in `try_parse_response`
  (same scan that handles Content-Length/TE/Connection — a
  "Retry-After:" inside another header's VALUE does not match).
- Delta-seconds form only; an HTTP-date value parses as absent
  (0). Duplicate headers: last wins (not a framing header, so no
  smuggling ambiguity — unlike Content-Length).
- Saturation, never wrap: parse bounded to 10 digits, clamped to
  4294967 s (× 1000 ms still fits uint32; CWE-190 family).
- New accessor `otlp_http_request_retry_after_ms()`.

**Exporter** (`src/exporter.c`):
- `record_outcome()` takes the Retry-After value (read from the
  request BEFORE `otlp_http_request_free` — the value lives in
  the request's parsed-response state).
- Retry delay on 429/503/5xx = **max(jittered backoff,
  Retry-After)**, clamped to `backoff_max_ms`. Never sooner than
  the server asked; never stall-able past our own cap (a hostile
  server sending Retry-After: 86400 cannot park exports).
- Network-error path (no response) unchanged — there is no
  header to honor. Sync flush path unchanged by design: it treats
  non-2xx as permanent (no retry decision exists there).
- The retry WARN log annotates "(server Retry-After)" when the
  server value drove the delay.
- `exporter.h` documents the floor+clamp contract on
  `backoff_max_ms`.

**Incidental fixes found on the way** (real CI holes):
- Six side-effecting asserts across three test files
  (`otlp_exporter_emit_move` in test_exporter_retry.c ×4 — the
  ENTIRE retry suite was a vacuous no-op under Release/NDEBUG —
  plus metric_record in test_exporter_echo.c and the new test).
  All converted to statement + pure assert per the v0.5.82 rule.
- `test_helper_echo.h` was mangled by the v0.5.89 Windows-guard
  edit: `#define ECHO_RAW_RESPONSE (-1)` had been injected three
  extra times INSIDE the prototypes (compiled only because
  preprocessor directives vanish and identical redefinition is
  benign). Rewritten clean — one define, clean prototypes.

## Tests

- `test_http_parser.c`: +4 tests (14 total) — delta-seconds
  parse, absent/HTTP-date/in-other-header-value, duplicate
  last-wins, saturation.
- `test_exporter_retry_after.c` (new, real HTTP wire): case 1
  proves the floor (429 + Retry-After: 1 with jitter ~0 → retry
  arrives 1000ms later); case 2 proves the clamp (Retry-After: 60
  with backoff_max=300 → retry arrives at ~300ms, not 60s).
  Server-side wall-clock timestamps read after the echo worker's
  RELEASE exit store (happens-before edge, TSAN-clean).
- Debug + Release suites green (41/41 both) — the Release run is
  what exposed the vacuous-assert hole. ASAN clean (the only
  LSAN hit is the documented macOS Network-framework leak in
  property-url-parse).

## Remaining work

None for this TODO. Possible future: HTTP-date Retry-After form
(deprecated by RFC 9111 guidance in favor of delta-seconds;
collectors in practice send delta-seconds).
