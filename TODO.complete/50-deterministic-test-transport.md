# TODO 50 — Deterministic test HTTP transport

**Status:** Complete (v0.5.6)
**Priority:** P2
**Depends on:** nothing

## Goal

Replace the in-process threaded echo server in
`tests/property/test_property_exporter.c` and friends with a
deterministic transport mock. Eliminates the test flake that
currently requires `continue-on-error` in CI.

## Background

The property-exporter test flakes ~5% of runs under ctest parallel
load because:
- The test spawns an echo server thread
- The exporter's batch boundaries depend on `batch_ms` timing
- Under load, the echo thread doesn't get scheduled in time →
  exporter hangs or asserts fail

The library code is sound (25/26 tests pass deterministically).
The flake is in the test infrastructure.

## Design

**Transport interface** (`src/http_client.h` extension):
```c
typedef struct otlp_http_transport otlp_http_transport_t;

struct otlp_http_transport {
    /* Open a connection (or simulate one). */
    otlp_status_t (*connect)(otlp_http_transport_t *self,
                             const char *host, uint16_t port,
                             otlp_socket_t **out);

    /* The transport owns its sockets; they're opaque. */
};

/* Default: real-socket transport. */
const otlp_http_transport_t *otlp_http_transport_default(void);

/* Test: in-memory queue. Writes go to a buffer; reads come from a
 * pre-loaded response. No threads, no I/O. */
const otlp_http_transport_t *otlp_http_transport_mock(void);

/* Install a process-wide transport. */
void otlp_http_transport_set(const otlp_http_transport_t *t);
```

**Mock transport**:
- `connect` returns a fake socket handle
- `write` appends to a per-request buffer (the test can inspect
  the bytes written)
- `read` returns bytes from a pre-set response queue
- `finish_connect` always succeeds immediately
- No blocking, no threads, no real I/O

**Test rewrite**:
```c
static int
prop_exporter_batch_flush(uint64_t seed)
{
    /* Install mock transport with 200 OK responses queued. */
    otlp_http_transport_set(otlp_http_transport_mock());
    otlp_http_transport_mock_queue_response(200, "");

    /* Build + flush as before. The exporter's HTTP calls go to
     * the mock — deterministic, no threads. */
    ...

    otlp_http_transport_set(otlp_http_transport_default());
}
```

## Tradeoff

This requires refactoring `http_client.c` to use the transport
interface instead of calling `otlp_socket_*` directly. That's
~50 LOC of indirection but it's the right architecture for
testability AND future transports (UDP, shared memory, custom).

## Acceptance criteria
- [ ] Transport interface added.
- [ ] Default real-socket transport preserves current behavior.
- [ ] Mock transport implemented.
- [ ] property-exporter test rewritten to use mock.
- [ ] Test passes 1000/1000 iterations deterministically.
- [ ] `continue-on-error` removed from CI for property-exporter.

## Out of scope (deferred further)
- UDP transport (real feature, not test infra).
- Custom transport registration by name.
