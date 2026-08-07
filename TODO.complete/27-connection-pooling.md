# TODO 27 — HTTP/1.1 keep-alive + connection reuse

**Status:** Complete (v0.5)
**Priority:** P1
**Depends on:** nothing

## Goal

Eliminate per-POST TCP handshake + DNS lookup cost by caching one
connection per exporter between batches.

## What shipped (v0.5)

**Request-header change** (`src/http_client.c`):
- `Connection: close` → `Connection: keep-alive` (HTTP/1.1 default).

**Response parsing** (`src/http_client.c`):
- New `keepalive_eligible` flag on `otlp_http_request_t`.
- Parser scans response for `Connection: close`; if present, flag is
  cleared. HTTP/1.1 default (no Connection header) keeps the flag set.
- If response had no Content-Length, framing is ambiguous — flag is
  cleared defensively.

**New public internal APIs** (`src/http_client.h`):
- `otlp_http_request_start_with_socket(out, url, ua, body, len, sock)`
  — accepts a previously-detached socket; request enters SENDING
  directly (skips CONNECTING).
- `otlp_http_request_detach_socket(req)` — extracts the socket from
  a DONE-state request, returning NULL if `keepalive_eligible` is
  false. Caller now owns the socket.

**Exporter integration** (`src/exporter.c`, `src/exporter_otel.{h,c}`):
- New `keepalive_sock` field on `struct otlp_exporter`.
- `try_start_post` passes the cached socket (if any) to
  `otlp_exporter_otel_build_request`, which donates it to the new
  http_request via `_start_with_socket`.
- On DONE, the exporter calls `_detach_socket` and caches the result.
- On non-2xx or network error, the cached socket (if any) is closed
  to avoid reusing a half-broken connection.
- `otlp_exporter_free` closes any still-cached socket.

**Property tests** (`tests/property/test_property_keepalive.c`):
- `prop_keepalive_disabled_on_explicit_close` — response with
  `Connection: close` makes detach return NULL.
- `prop_keepalive_eligible_by_default` — response without Connection
  header allows detach.
- `prop_keepalive_reuse_roundtrip` — detach + donate socket to a
  second request; verify it completes against the same server
  connection.

Uses a minimal inline TCP server (no echo helper) so the test
controls the exact response bytes — including whether Connection:
close is sent.

## Design notes

The exporter is single-threaded for I/O (caller-tick model, one
in-flight request at a time). So a pool of 1 connection is
sufficient for v1.0. The implementation caches exactly one socket
per exporter; multi-connection pools are deferred.

Stale-connection handling: when a cached socket is reused and the
server has closed it, `_step` returns an error on the first write or
read. The exporter's failure path frees the request (which closes
the socket) and retries with a fresh connection on the next tick.
No explicit probe-read is needed.

## Acceptance criteria
- [x] CI green on macOS arm64 (build verified locally).
- [x] No regression in existing tests (all 16 property + 4 unit
      + 1 stress + 1 smoke + 1 integration pass).
- [x] New property tests pass (3 keepalive properties).
- [x] Existing exporter echo + retry tests still pass (they use the
      `Connection: close` echo server, so pooling is disabled
      end-to-end, but the request path is exercised).

## Out of scope (deferred)
- Multi-connection pool (currently 1 cached socket per exporter).
- Idle timeout (close cached socket after N seconds of inactivity).
- Connection probe (read-with-timeout before reuse to detect
  half-open connections proactively).
- HTTP/2 multiplexing (would require HTTP/2 client).
