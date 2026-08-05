# Security review — v0.1.x

This document captures the security audit of `otlp-c` v0.1.x. It
covers what was reviewed, what was found, and what's tracked as
follow-up. The audit is internal (no external review yet); external
review is scheduled for v1.0 (TODO 19 / `TODO.complete/phase-19-security-hardening.md`).

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

For v0.1.x we assume the sidecar is trusted (same host). The
network attacker is not in scope — plain HTTP on localhost only.
A hostile caller is partially in scope (input validation).

## Surface

### Public API (`include/otlp-c/`)

- `otlp_version()` — no input; returns static string. Safe.
- `otlp_strerror()` — switch on enum; returns static string. Safe.
- `otlp_span_create(name)` — mallocs; copies `name`. Bounds-checks
  `name == NULL` (treated as "").
- `otlp_span_set_*` — fixed-size memcpy (16B / 8B IDs); string/bytes
  copy with malloc. NULL-checked.
- `otlp_span_set_attribute_*` — fixed-cap array (128). Returns
  `OTLP_ERR_OVERFLOW` past cap. No truncation; safe.
- `otlp_tracer_*` — no input from caller beyond strings (copied).
- `otlp_exporter_emit()` / `_emit_move()` — receives a span. Deep-
  clones or moves. NULL-checked.
- `otlp_exporter_tick()` / `_flush()` / `_shutdown()` — no caller input.
- `otlp_exporter_get_stats()` — writes to caller-provided struct.
  NULL-checked.

### Internal API (`src/`)

- Protobuf encoder — bounded by `struct otlp_pb_buf` cap (SIZE_MAX/2).
  Growth is doubling. Overflow-checked.
- HTTP client — URL parser rejects `https://`, ports > 65535, missing
  host. The parser uses fixed-size `host[256]` / `path[128]` buffers
  with bounds checks.
- MPSC queue — bounded by user-provided capacity. No overflow on
  sequence numbers (uint64 with Vuykov's monotonic invariant).
- Exporter — atomic stats, fixed pending array, single in-flight
  request. No unbounded growth.

### Platform abstraction

- `otlp_socket_*` — non-blocking sockets. `getaddrinfo` is called
  synchronously (one-shot per request). DNS poisoning / rebinding
  is a concern if the sidecar's hostname resolves to an attacker.
  Documented in `docs/deployment.md`.
- `otlp_platform_now_*` — wraps `clock_gettime` / `QueryPerformanceCounter`.
  No security surface.

## Findings

### HIGH severity — none.

### MEDIUM severity — none in v0.1.x (with the trusted-sidecar assumption).

### LOW severity

1. **`otlp_http_parse_url` accepts any host** — including link-local,
   broadcast, RFC1918 private addresses. The library doesn't try to
   block SSRF. Acceptable for the trusted-sidecar topology; the
   caller controls the endpoint. Document this in `SECURITY.md`
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

## Recommendations for v0.2.x

- Add a property test that fuzzes the URL parser with malformed
  inputs (no host, port=0, port=65536, scheme missing, etc.).
- Add a property test that fuzzes the HTTP response parser with
  truncated / oversized responses.
- Replace the 30s hard cap in `flush()` with a configurable option.

## Recommendations for v1.0

- External security review (Ribose security team).
- fuzz-introspector / libFuzzer harness for the encoder + HTTP
  parser.
- Consider adding a `localhost-only` default endpoint assertion
  (warn if the configured endpoint is not 127.0.0.1).

## Vulnerability disclosure

See `SECURITY.md` for private disclosure to security@ribose.com.
