# Security review — v0.5.x

This document captures the security audit of `otlp-c`. The original
review was performed at v0.1.x; this revision refreshes it for the
v0.5.x surface (metrics, logs, context propagation, sampler, slab
allocator). It covers what was reviewed, what was found, and what
is tracked as follow-up. External review is scheduled for v1.0
(TODO 19 / `TODO.complete/phase-19-security-hardening.md`).

## Threat model

`otlp-c` is a library that runs inside a host application. The
application's trust boundary includes the library. The library
talks plain HTTP to `localhost:4318` (the otelcol sidecar).

**Assets inside the trust boundary:**
- Application memory (the library mallocs, frees, never reads outside).
- Application control flow (the library never calls back into the
  application except via documented callbacks).

**Threats outside the trust boundary:**
- A malicious or compromised otelcol sidecar.
- A network attacker between the library and the sidecar.
- A malicious caller (the application itself is hostile).

For v0.5.x we assume the sidecar is trusted (same host). The
network attacker is not in scope — plain HTTP on localhost only.
A hostile caller is partially in scope (input validation).

## Surface

### Public API (`include/otlp-c/`) — traces

- `otlp_version()` — no input; returns static string. Safe.
- `otlp_strerror()` — switch on enum; returns static string. Safe.
- `otlp_span_create(name)` — mallocs; copies `name`. Bounds-checks
  `name == NULL` (treated as "").
- `otlp_span_set_*` — fixed-size memcpy (16B / 8B IDs); string/bytes
  copy with malloc. NULL-checked.
- `otlp_span_set_attribute_*` — fixed-cap array (128). Returns
  `OTLP_ERR_OVERFLOW` past cap. No truncation; safe.
- `otlp_span_add_event` / `otlp_span_add_link` — fixed-cap arrays
  (64 each); per-event/link attribute cap is 32. Returns
  `OTLP_ERR_OVERFLOW` past cap.
- `otlp_tracer_*` — no input from caller beyond strings (copied).
  PRNG state is lock-free (`<stdatomic.h>` via `atomic_compat.h` on
  MSVC); CAS-protected against concurrent ID generation.
- `otlp_exporter_emit()` / `_emit_move()` — receives a span. Deep-
  clones or moves. NULL-checked. Pushed onto a bounded MPSC queue;
  overflow returns `OTLP_ERR_BUFFER_FULL`.
- `otlp_exporter_tick()` / `_flush()` / `_shutdown()` — no caller input.
- `otlp_exporter_get_stats()` — writes to caller-provided struct.
  NULL-checked.

### Public API — metrics (`metric.h`)

- `otlp_metric_create` — string copied; NULL-checked.
- `otlp_metric_add_data_point_*` — fixed-cap array (1024 points
  per metric). Returns `OTLP_ERR_OVERFLOW` past cap. Quantile /
  bucket / explicit-bound arrays within a data point are fixed-cap
  too.
- `otlp_exporter_flush_metric()` — synchronous encode + POST. Uses
  the same HTTP state machine as traces.

### Public API — logs (`log.h`)

- `otlp_log_create` — string/body copied; NULL-checked.
- `otlp_exporter_flush_log()` — synchronous encode + POST.

### Public API — context propagation (`context.h`)

- `otlp_context_*` — fixed-size buffers for `traceparent` (55 bytes)
  and `tracestate` (512 bytes). Inject/extract operate via
  caller-provided carrier callbacks (get/set/foreach). The library
  never reads past what the carrier returns.
- Tracestate parser rejects oversized fields and returns
  `OTLP_ERR_INVALID_ARGUMENT` rather than truncating silently.

### Public API — sampler (`sampler.h`)

- `otlp_sampler_always_on` / `_always_off` — static singletons; no
  state, no input beyond the span data passed for the decision.
- `otlp_sampler_trace_id_ratio_based_create` — takes a `ratio`
  param; clamped to `[0.0, 1.0]`. Free at any time; the caller owns
  the handle.

### Public API — slab allocator (`slab.h`)

- `otlp_slab_create(slot_size, capacity)` — allocates a contiguous
  arena + a LIFO free-list stack. NULL-checked.
- `otlp_slab_alloc_ptr` / `otlp_slab_free_ptr` — O(1) stack ops.
  Alloc returns NULL on full. Free checks the pointer is within the
  arena's address range; out-of-range pointers fall through to
  libc `free` (so the slab can coexist with regular malloc traffic).
- `otlp_install_slab_allocator(slot_size, capacity)` — installs the
  slab as the process-wide `otlp_malloc`/`otlp_free` backend via
  `otlp_set_allocator`. After installation, every `otlp_malloc` call
  routes through the slab's arena-range check, falling back to libc
  `malloc` for sizes that don't match the slot size. **Threat-model
  note:** a hostile caller installing the slab then freeing a
  non-slab pointer is caught by the address-range check (routed to
  libc `free`). The slab never accepts an arbitrary external
  pointer as one of its own slots. Uninstall via
  `otlp_set_allocator(NULL, NULL)` restores the libc default.

### Internal API (`src/`)

- Protobuf encoder — bounded by `struct otlp_pb_buf` cap (SIZE_MAX/2).
  Growth is doubling. Overflow-checked.
- HTTP client — URL parser rejects `https://`, ports > 65535, missing
  host. The parser uses fixed-size `host[256]` / `path[128]` buffers
  with bounds checks.
- MPSC queue — bounded by user-provided capacity. No overflow on
  sequence numbers (uint64 with Vyukov's monotonic invariant).
- Exporter — atomic stats (`otlp_atomic_u64` via `atomic_compat.h`),
  fixed pending array, single in-flight request. No unbounded growth.
- Schema-driven field tables (`otlp_schema.h`) — looked up by enum
  index, not user string. No path for a caller to corrupt the table.

### Platform abstraction

- `otlp_socket_*` — non-blocking sockets. `getaddrinfo` is called
  synchronously (one-shot per request). DNS poisoning / rebinding
  is a concern if the sidecar's hostname resolves to an attacker.
  Documented in `docs/deployment.md`.
- `otlp_platform_now_*` — wraps `clock_gettime` / `QueryPerformanceCounter`.
  No security surface.

## Findings

### HIGH severity — none.

### MEDIUM severity — none in v0.5.x (with the trusted-sidecar assumption).

### LOW severity

1. **`otlp_http_parse_url` accepts any host** — including link-local,
   broadcast, RFC1918 private addresses. The library doesn't try to
   block SSRF. Acceptable for the trusted-sidecar topology; the
   caller controls the endpoint. Documented in `SECURITY.md`
   threat-model section.

2. **Integer overflow in `mpsc_queue_push`** — the diff calculation
   uses signed arithmetic on uint64 values. Verified safe under
   realistic inputs; would need ~584 years at 100M spans/sec to
   roll over. Documented in `src/mpsc_queue.c`.

3. **`otlp_exporter_flush` has a 30s hard cap** — magic number. If
   a caller needs longer, they must call `tick()` in a loop. Documented
   in `include/otlp-c/exporter.h`.

4. **HTTP response body unbounded up to 64KB** — `OTLP_HTTP_RESP_MAX`
   cap. A malicious collector could send a 64KB body and the library
   would buffer it. Acceptable for trusted collectors; the cap is
   low enough to be DoS-safe.

5. **No TLS** — by design (ADR 0004). The sidecar handles TLS. The
   library never sends data over the public internet.

## Recommendations — completed

The v0.1.x review listed the following for v0.2.x; their current
status:

- **Property test that fuzzes the URL parser** — Done.
  `tests/property/test_property_url_parse.c`. Documented in
  `TODO.complete/phase-03-http-client.md`.
- **Property test that fuzzes the HTTP response parser** — Done.
  `test_http_echo_state_machine` and `test_http_status_codes` in
  `tests/test_http_echo.c`. Truncated-oversized-response coverage
  added in TODO 50.
- **Configurable `flush()` cap** — Open. The 30s cap remains
  hardcoded; callers wanting longer loop `tick()`. Tracked as a
  post-1.0 polish item.

The v0.5.x sanitizer work (ASAN + UBSAN + TSAN in CI; see
`.github/workflows/ci.yml`) covers continuous fuzz-class coverage
for the encoder, HTTP parser, and MPSC queue.

## Recommendations for v1.0

- External security review (Ribose security team).
- fuzz-introspector / libFuzzer harness for the encoder + HTTP
  parser.
- Consider adding a `localhost-only` default endpoint assertion
  (warn if the configured endpoint is not 127.0.0.1).
- Audit the slab allocator's `otlp_install_slab_allocator` path
  under TSAN with concurrent install + free, to verify the
  arena-range check is race-free in all orderings.

## Vulnerability disclosure

See `SECURITY.md` for private disclosure to security@ribose.com.
