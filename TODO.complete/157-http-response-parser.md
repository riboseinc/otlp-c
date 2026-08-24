# TODO 157 — Extract the HTTP response parser (deep module)

**Status:** Complete (v0.6.11)
**Priority:** P1 (security-critical surface had a platform coverage gap)

## The friction

`try_parse_response` (~200 lines) and `decode_chunked_in_place`
were `static` captives of the socket state machine in
`http_client.c`. Their only test surface was TCP: every parse
scenario needed a pthread echo server over loopback, and the
response fuzz property was `#if !defined(_WIN32)` — the
RFC 7230 request-smuggling rejections (TE+CL, duplicate-CL,
chunked framing) had **zero coverage on the Windows CI job**.

## What shipped

- **`src/http_response_parser.{h,c}`** — the parser as a deep
  module: `otlp_http_resp_parse(buf, len, at_eof, out)` →
  `1 done / 0 need-more / -1 malformed`, outputs (status,
  Retry-After ms, keep-alive verdict, zero-copy body) written
  only on DONE. No sockets, no clocks, no allocation. The
  MSVC/POSIX `strncasecmp` compat shim moved with its only user.
- **`http_client.c` shrank by ~307 lines**: `step_reading` now
  feeds the parser through a 10-line adapter; buffer growth and
  timeouts stay socket-side. `OTLP_HTTP_RESP_MAX` now lives in
  the parser header (it IS the response-size contract).
- **`tests/unit/test_unit_http_response_parser.c`** (test #50):
  byte fixtures for the full matrix — status-line forms, split
  delivery invariance (every prefix of a response must be
  need-more-or-agree), version-aware keep-alive, EOF framing,
  all smuggling rejections, line-aligned header matching,
  case-insensitive names, chunked (multi-chunk, extensions,
  trailers, partial, corrupt), Retry-After (seconds, saturation,
  HTTP-date-as-absent), oversized CL. Pure — runs on every
  platform including the Windows CI job.
- **Response fuzz is now pure and portable**: feeds bytes
  straight to the parser, no echo server, no `#WIN32` guard;
  iterations raised 300 → 20000 (was ~50 ms/iter through a
  socket, now microseconds); verdict + body-bounds invariants
  asserted on every case.

## Verification

- 50/50 in Debug, Release (zero warnings), ASAN
  (`detect_leaks=1`), UBSAN.
- **Both directions mutation-tested**: disabling the
  duplicate-CL rejection fails the unit test; weakening the fuzz
  verdict assertion aborts the property.
- ASAN caught a real bug in the first cut of the new test
  (fixed-size `memcpy` from shorter string literals —
  over-read); rewritten with computed lengths.

## Deletion test

Deleting the module moves ~300 lines of wire-format logic back
into the socket machine and re-couples every parser test to TCP
threads — complexity re-concentrates. Deep module confirmed.
