# Input validation audit — v0.2.x

Boundary-by-boundary audit of input validation at every public API
entry point. Documents what's checked, what's trusted, and what's
deferred.

## Methodology

For each public function in `include/otlp-c/`, enumerated:
1. Inputs from the caller.
2. What the function validates.
3. What it trusts.
4. Failure mode on bad input.

## Findings by header

### `otlp.h`

- `otlp_version()` — no input. Returns static string. **Safe.**

### `status.h`

- `otlp_strerror(code)` — switch on enum; default returns "unknown
  status". **Safe.** No bound on the input integer (negative values
  are clamped by the switch's default case).

### `span.h`

- `otlp_span_create(name)` — NULL → treated as `""`. Otherwise
  `strlen` + `malloc(len+1)` + `memcpy`. **Safe.** No length cap;
  a multi-MB name would allocate a multi-MB buffer. *Recommendation:
  add a sanity cap at 64KB?* Defer — caller controls.

- `otlp_span_set_trace_id(span, trace_id)` — NULL span →
  `OTLP_ERR_NULL`. NULL trace_id → `OTLP_ERR_NULL`. Otherwise
  fixed-size `memcpy(16)`. **Safe.** No content validation (caller
  may pass all-zero; library doesn't reject).

- `otlp_span_set_attribute_string(span, key, value)` — NULL span or
  NULL key → `OTLP_ERR_NULL`. NULL value → treated as `""`. Otherwise
  two `malloc`s (key + value) + memcpy. **Safe.** No key/value
  length cap; same multi-MB concern. *Defer.*

- `otlp_span_set_attribute_bytes(span, key, bytes, len)` — NULL
  span/key → `OTLP_ERR_NULL`. `len > 0 && bytes == NULL` →
  `OTLP_ERR_NULL`. Otherwise malloc(len) + memcpy. **Safe.** `len`
  is `size_t`; SIZE_MAX would overflow the malloc — but the user
  can't realistically pass that.

- `otlp_span_set_attribute_int/double/bool` — NULL span/key →
  `OTLP_ERR_NULL`. Otherwise fixed-size store. **Safe.**

- `otlp_span_set_status(span, code, description)` — NULL span →
  `OTLP_ERR_NULL`. NULL description → status set without message.
  Otherwise malloc + memcpy. **Safe.** `code` is an enum; out-of-
  range values would store as-is (no validation).

- `otlp_span_add_event / _link / set_trace_state` — stubs returning
  `OTLP_ERR_NOT_IMPLEMENTED`. NULL span → `OTLP_ERR_NULL`. **Safe.**

- `otlp_span_free(span)` — NULL-safe no-op. **Safe.**

### `tracer.h`

- `otlp_tracer_create(service_name, scope_name, scope_version)` —
  NULL → treated as `""`. Otherwise 3 mallocs + memcpy. **Safe.**
- `otlp_tracer_start_span(tracer, name)` — NULL tracer → returns
  NULL. **Safe.** Generates 16-byte trace ID + 8-byte span ID via
  xorshift64s; rejects all-zero and regenerates.
- `otlp_tracer_start_child_span(tracer, name, parent)` — NULL parent
  → returns NULL. Otherwise copies parent's trace ID + sets parent
  link. **Safe.**

### `exporter.h`

- `otlp_exporter_create(opts)` — NULL opts → returns NULL. Otherwise
  normalizes defaults (NULL endpoint → "http://localhost:4318/v1/traces",
  NULL service_name → "", zero numeric fields → library defaults),
  copies strings, parses URL. **Safe.** URL parse failure → returns
  NULL.

- `otlp_exporter_emit(exp, span)` — NULL exp/span → `OTLP_ERR_NULL`.
  Shutdown requested → `OTLP_ERR_SHUTDOWN`. Otherwise deep-clones
  span (which is independently validated) and pushes to MPSC queue.
  Queue full → `OTLP_ERR_BUFFER_FULL`. **Safe.**

- `otlp_exporter_emit_move(exp, span)` — same as emit but takes
  ownership. On failure (queue full), frees the span. **Safe** but
  caller must not touch span after this call regardless of return
  value (documented).

- `otlp_exporter_tick / flush / shutdown` — NULL → `OTLP_ERR_NULL`.
  Otherwise operates on internal state. **Safe.**

- `otlp_exporter_poll_fds(exp, out, cap, n_out)` — NULL exp/n_out →
  `OTLP_ERR_NULL`. `cap == 0` → returns 0 fds. **Safe.**

- `otlp_exporter_get_stats(exp, out)` — NULL → `OTLP_ERR_NULL`.
  Otherwise fills out via atomic loads. **Safe.**

### `http_client.h` (internal)

- `otlp_http_parse_url(url, out)` — NULL → `OTLP_ERR_NULL`. Scheme
  != "http://" → `OTLP_ERR_INVALID_ARGUMENT`. Missing host →
  `OTLP_ERR_INVALID_ARGUMENT`. Port > 65535 →
  `OTLP_ERR_INVALID_ARGUMENT`. **Safe.** Fixed-size host[256] /
  path[128] buffers; bounds-checked before copy.

## Trust boundaries

The library trusts:
- The caller to pass valid pointers (NULL-checked everywhere).
- The caller to own the memory passed in for the duration of the
  call (documented per-function).
- The configured endpoint URL to be syntactically valid (validated
  at `otlp_exporter_create`).
- The local otelcol sidecar to be reachable and well-behaved (per
  ADR 0004 / SECURITY-ASSESSMENT.md).

The library does NOT trust:
- Network input (HTTP responses are bounds-checked, capped at
  64 KB body).
- The MPSC queue's consumers (single-consumer invariant enforced
  by design; concurrent tick() calls would race but the docstring
  forbids it).

## Recommendations

1. **Add a sanity cap on string attribute length** (e.g. 64 KB) to
   prevent accidental OOM from a buggy caller. v0.3+ work.
2. **Add a property test that fuzzes every public setter with NULL /
   huge / malformed inputs**. v0.3+ work.
3. **External review** of the HTTP response parser for malformed
   inputs (chunked encoding, missing Content-Length, oversized
   headers). v1.0 blocker.

## Verdict

Input validation is comprehensive at the public API surface. NULL
checks are universal. Length bounds are fixed where the protocol
defines them (IDs) and uncapped where the caller controls them
(strings, bytes). The MPSC queue is bounded by user-configurable
capacity. No HIGH-severity gaps found.
