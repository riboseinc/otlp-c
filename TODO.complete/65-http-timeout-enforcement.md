# TODO 65 — HTTP connect/read timeout enforcement

**Status:** Complete (v0.5.25)
**Priority:** P0 (production reliability)

## What shipped (v0.5.25)

### Fixed: connect_timeout_ms / read_timeout_ms were dead config

The exporter opts had two timeout fields since the beginning:

```c
uint32_t connect_timeout_ms;  /* Default: 5000 (5s). */
uint32_t read_timeout_ms;     /* Default: 10000 (10s). */
```

These were:
1. Documented in the header as functional ("Connect timeout in
   milliseconds", "Read timeout in milliseconds").
2. Normalized in `otlp_exporter_create` (defaults applied).
3. **Never stored in the exporter struct.**
4. **Never passed to the HTTP client.**
5. **Never enforced anywhere.**

The HTTP state machine (`src/http_client.c`) had no concept of
timeouts. `step_connecting` polled `finish_connect` forever.
`step_reading` polled `socket_read` forever. The only thing that
bounded the wait was the TCP stack's own connect timeout (60-120s
on Linux/macOS) or the caller's `tick(max_wait_ms)` / `flush()`
deadline.

Impact: if the collector was unreachable (network partition,
wrong host, firewall drop), `tick()` blocked for up to
`flush_timeout_ms` (30s default) per failed request. For a
caller driving tick() from its event loop, this stalls the
entire process for 30 seconds.

### The fix

**1. HTTP client timeout surface** (`src/http_client.{h,c}`)

`otlp_http_request_start` and `_start_with_socket` (internal
API) now accept `connect_timeout_ms` and `read_timeout_ms`:

```c
otlp_status_t otlp_http_request_start(
    otlp_http_request_t **out,
    const struct otlp_http_url *url,
    const char *user_agent,
    const uint8_t *body, size_t body_len,
    uint32_t connect_timeout_ms,
    uint32_t read_timeout_ms);
```

0 means no timeout (infinite) — backward compatible for tests
and callers with their own deadline logic.

**2. Request struct stores deadline info**

```c
struct otlp_http_request {
    ...
    uint32_t connect_timeout_ms;
    uint32_t read_timeout_ms;
    uint64_t start_ms;       /* monotonic ms at connect initiation */
    uint64_t last_recv_ms;   /* monotonic ms at most recent recv */
};
```

**3. Deadline checks in step functions**

`step_connecting`: after each WOULDBLOCK, checks if
`now - start_ms >= connect_timeout_ms`. If so, transitions to
FAILED and returns `OTLP_ERR_TIMEOUT`.

`step_reading`: after each WOULDBLOCK, checks if
`now - last_recv_ms >= read_timeout_ms`. If so, transitions to
FAILED. `last_recv_ms` is reset on each successful recv, so a
slow-but-steady stream doesn't time out — only inter-recv
silence does.

**4. Timing: clock starts after DNS, not at function entry**

`start_ms` is set AFTER `otlp_socket_connect` (which calls the
blocking `getaddrinfo`) returns, not at the start of
`otlp_http_request_start`. The blocking DNS lookup can take
seconds (especially for unresolvable hosts); measuring the
connect timeout from before it would make the deadline fire
prematurely. The connect timeout measures TCP connect time only.

**5. Exporter stores and passes timeouts**

`struct otlp_exporter` now has `connect_timeout_ms` and
`read_timeout_ms` fields, stored at create time from the
normalized opts. All call paths pass them through:

- `otlp_exporter_otel_build_request` (traces) → accepts and
  forwards both timeouts.
- `flush_sync` (metrics/logs) → passes both timeouts to
  `otlp_http_request_start`.

All 9 call sites of `_start` / `_start_with_socket` updated
(2 in exporter_otel, 1 in exporter, 1 in test_http_echo, 4 in
test_property_keepalive, 1 in test_integration_jaeger).

### Test

`tests/property/test_property_http_timeout.c`: starts a request
to `192.0.2.1` (RFC 5737 TEST-NET-1, IANA-reserved for
documentation, guaranteed to never be routed) with
`connect_timeout_ms=200`. Drives `step()` in a loop and asserts
the request reaches FAILED within 5 seconds.

On typical systems, the non-blocking connect to 192.0.2.1
returns EINPROGRESS (SYN sent, no response). After 200ms, the
deadline check fires and transitions to FAILED. Without the
fix, the request would hang for 60+ seconds (TCP stack timeout).

POSIX-only (uses `clock_gettime` for timing).

## Why this matters

A library that claims "non-blocking, caller-driven" must
actually bound its blocking operations. The HTTP timeouts are
the last line of defense against a broken network. Without
them, every tick() call can stall the caller's event loop for
the full TCP timeout period (60-120 seconds). With them, the
caller gets `OTLP_ERR_TIMEOUT` within the configured window and
can retry, back off, or report the error.

This is especially important for the caller-driven model: the
caller calls `tick(exp, max_wait_ms)` expecting it to return
within `max_wait_ms`. If the HTTP step inside tick blocks for
60 seconds, `max_wait_ms` is meaningless. The timeout
enforcement makes `max_wait_ms` actually work.

## Acceptance criteria
- [x] `connect_timeout_ms` + `read_timeout_ms` stored in exporter struct.
- [x] Passed through to HTTP client in all code paths.
- [x] HTTP step enforces connect deadline in CONNECTING state.
- [x] HTTP step enforces read deadline in READING state.
- [x] Read deadline resets on each successful recv.
- [x] Deadline clock starts after DNS, not at function entry.
- [x] 0 timeout = infinite (backward compat for existing tests).
- [x] 32/32 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
- [x] New property test verifies bounded completion on unreachable host.
