# TODO 03 — HTTP/1.1 client + non-blocking platform I/O

**Status:** Complete
**Phase:** 3
**Priority:** P0
**Branch:** `phase-3-http-client`

## Goal

URL parsing, non-blocking socket primitives (POSIX + Win32), and a non-blocking HTTP/1.1 POST state machine driven by the caller through `otlp_http_request_step()`. No library threads, no mutexes. Foundation for Phase 5's caller-tick exporter.

## Acceptance criteria

- [ ] `src/platform.h` extended with non-blocking socket surface. **No thread/mutex declarations.**
- [ ] `src/platform_unix.c` (POSIX) and `src/platform_win.c` (Win32) implement the surface; `platform.c` retains shared clock code.
- [ ] `src/http_client.{h,c}` exposes `otlp_http_request_t` as a non-blocking state machine: `start` → `step` → terminal state.
- [ ] URL parse: accept only `http://`; reject `https://`, missing host, port > 65535, etc. with `OTLP_ERR_INVALID_ARGUMENT`, never crash.
- [ ] `otlp_http_request_step` returns `OTLP_OK` on DONE, `OTLP_ERR_WOULDBLOCK` when caller should poll, terminal errors otherwise.
- [ ] Status line parsed correctly (200/4xx/5xx/malformed → correct `http_status`).
- [ ] Property tests for URL parsing (valid + invalid).
- [ ] Unit test against in-process echo server (test-only thread, not in the library).
- [ ] CI green on Linux x86_64, macOS arm64, macOS x86_64, Windows x64.

## Files

- `src/platform.h` — extend.
- `src/platform.c` — keep clock code, remove if-empty.
- `src/platform_unix.c` — new.
- `src/platform_win.c` — new.
- `src/http_client.h` — new internal header.
- `src/http_client.c` — replace stub.
- `CMakeLists.txt` — pick platform source by OS; link `Ws2_32` on Windows.
- `tests/property/test_property_url_parse.c` — new.
- `tests/test_http_echo.c` — new (unit, in-process echo server).
- `tests/CMakeLists.txt` — register.
- `tests/test_helper_echo.{h,c}` — new (reusable echo server for tests).

## Test plan

- `prop_url_parse_valid`: PRNG-generated host/port/path; round-trip parse + reconstruct.
- `prop_url_parse_invalid`: malformed URLs rejected with `OTLP_ERR_INVALID_ARGUMENT`.
- `test_http_echo_state_machine`: echo server returns body unchanged; drive `_step()` in a `poll` loop; assert DONE + correct body.
- `test_http_status_codes`: canned status lines (200, 404, 500, malformed); assert correct `http_status` extraction.

## Architectural decisions

- DNS is blocking `getaddrinfo` (one-shot per request, cached per exporter for v0.1.0). Non-blocking DNS is post-1.0.
- Always `Connection: close` (no keepalive in v0.1.0).
- Single request at a time per exporter in v0.1.0; multiple in-flight deferred.

## Dependencies

- Phase 0 only (independent of Phases 1-2).

## Verification

```
cmake --build build
ctest --test-dir build --output-on-failure
ctest --test-dir build -R http --output-on-failure
```
