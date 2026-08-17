# TODO 120 — HTTP response-parser audit: chunked + framing hardening

**Status:** Complete (v0.5.80)
**Priority:** P1 (interop: chunked responses unusable; security: smuggling vectors unguarded)

## What shipped

The deep audit of the HTTP response parser (untrusted server
input; the deployment doc's sidecar topology means nginx/envoy
grade responses) found five issues:

**1. `Transfer-Encoding: chunked` was not handled at all (the
headline).** The parser only knew Content-Length and
read-until-EOF. A chunked response therefore:
- on keep-alive, waited for EOF that never comes → read timeout →
  the request fails and the batch burns its retries; every proxy
  that streams (nginx/envoy with buffering off) triggers this;
- even at EOF, the "body" contained the raw chunk framing (hex
  size lines between the payload).

Fixed with an in-place chunked decoder (RFC 7230 §4.1):
`<hex>[;ext] CRLF data CRLF` … `0 CRLF [trailers] CRLF`, decoded
by compacting data over the framing (dst ≤ src invariant — never
clobbers unread bytes), size-capped at OTLP_HTTP_RESP_MAX,
incremental (returns need-more until the terminator is buffered).
Chunked framing is self-delimiting, so the connection stays
keep-alive eligible.

**2. Header matching was substring-based, not line-aligned.** A
"Content-Length:" appearing inside another header's VALUE (e.g. an
echoed `X-Note: see Content-Length: 99`) would match and corrupt
framing. The scan now iterates header lines and matches at line
starts only.

**3. TE + Content-Length together** — the classic request-smuggling
vector (RFC 7230 §3.3.3) — is now rejected outright instead of
silently preferring one. Duplicate `Content-Length` with
*differing* values (smuggling, §3.3.2) is rejected; identical
duplicates still collapse.

**4. Undecodable Transfer-Encoding** (anything but bare `chunked`:
gzip, `gzip, chunked`, identity) is rejected — we cannot frame or
decode it, and pretending otherwise misparses.

**5. Keep-alive default ignored the HTTP version.** HTTP/1.0
responses (default close, RFC 7230 §6.3) were marked reusable →
the next request on the pooled socket fails. The default is now
version-aware (1.1+ persistent; 1.0 close unless `Connection:
keep-alive`), and all `Connection:` header lines are scanned, not
just the first.

The keepalive property test caught an index bug in the
version-parse during development — first run, exactly as designed.

## Tests

New `tests/test_http_parser.c` (9 tests) using a new
`ECHO_RAW_RESPONSE` mode in the echo helper (the handler supplies
the complete raw response bytes): chunked single chunk; chunked
multi-chunk + extensions + trailers; chunked 3 KB body (TCP
delivers it in segments — incremental path); TE+CL rejected;
duplicate differing CL rejected; gzip TE rejected;
header-value-not-matched; HTTP/1.0 not detachable;
case-insensitive header names.

## Sites changed

- `src/http_client.c` — `decode_chunked_in_place`; line-aligned
  header scan; TE/CL conflict rules; version-aware keep-alive.
- `tests/test_helper_echo.{h,c}` — `ECHO_RAW_RESPONSE`.
- `tests/test_http_parser.c`, `tests/CMakeLists.txt` — new suite.

## Verification

```
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout          # 37/37
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -E "http-timeout|url-parse"  # clean
```
