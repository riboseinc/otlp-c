# TODO 164 — Extra HTTP headers + OTEL_EXPORTER_OTLP_HEADERS

**Status:** Complete (v0.7.2)
**Priority:** P1 (feature: collector auth)

## Feature 1: arbitrary HTTP headers on export requests

- Public `otlp_http_header_t {name, value}` +
  `otlp_exporter_opts_t.http_headers/n_http_headers` (additive).
- Flow: opts → create() deep-copy (one blob, opts contract holds;
  CR/LF rejected → NULL) → SIGNAL_SPECS build fns →
  otlp_http_request_start(_with_socket) → build_request.
- build_request rewritten: incremental head assembly with
  per-append bounds checks; headers land between User-Agent and
  Content-Type; CRLF re-checked in the builder (the user_agent
  CWE-93 posture extended to every caller-controlled header
  field).
- Sync flush path carries them too.

## Feature 2: OTEL_EXPORTER_OTLP_HEADERS

"k=v,k=v" via the shared tokenizer extracted from the
resource-attrs parser (one engine, two typed sinks — DRY);
storage in otlp_env_storage_t (hdr names/values + array). The
env-var matrix is now complete for everything this library's
model expresses.

## Tests

- unit-env-config: env parsing (multi-pair, malformed skip, '=' in
  value, exporter round trip through the deep copy).
- http-parser wire tests (14 total now): the echo helper
  captures the FULL served request (headers included; worker
  writes, echo_server_join provides happens-before) — asserts
  headers on the wire in position, and injection attempts
  rejected before any bytes are sent.
- **Mutation-tested**: removing the CRLF scan in build_request
  fails the injection test at its line.

## Verification

52/52 via every preset (default/release/asan/ubsan/tsan);
Doxygen zero warnings; Release zero warnings.
