# TODO 92 — HTTP header injection hardening (CWE-93)

**Status:** Complete (v0.5.52)
**Priority:** P0 (security: CWE-93 HTTP request splitting)

## What shipped

Found and closed two HTTP header injection vectors:

1. **URL parser accepted CR/LF in host and path.** A caller-
   controlled URL containing `\r\n` could inject arbitrary
   HTTP headers into the request line or Host header.

2. **`build_request` interpolated user_agent without validation.**
   A caller-controlled user_agent containing `\r\n` could inject
   arbitrary HTTP headers via the User-Agent line.

Both are CWE-93 (HTTP request splitting) class. Real-world
vectors include config-file interpolation of endpoint strings
and runtime-derived user_agent values that aren't sanitized.

## Sites changed

- `src/http_client.c::otlp_http_parse_url` — pre-scan the entire
  URL for `\r` or `\n` before any further parsing; reject with
  `OTLP_ERR_INVALID_ARGUMENT`.
- `src/http_client.c::build_request` — scan `user_agent` for
  `\r` or `\n`; reject with `OTLP_ERR_INVALID_ARGUMENT`.
- `tests/property/test_property_url_parse.c` — added
  `prop_url_rejects_crlf` and `prop_user_agent_rejects_crlf`.

## Why defense in depth

URL validation covers `url->host` and `url->path`. User-agent
validation covers the remaining caller-supplied header field.
Together they close all header-injection vectors in the
outgoing POST request:

```
POST <url->path> HTTP/1.1     ← url->path (validated at parse)
Host: <url->host>             ← url->host (validated at parse)
User-Agent: <user_agent>      ← user_agent (validated at build)
Content-Type: application/x-protobuf  ← constant
Content-Length: <body_len>    ← integer
Connection: keep-alive        ← constant
```

The library's service_name is NOT in any HTTP header — it's a
resource attribute in the protobuf body. So no injection risk
from service_name.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 46/46 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

## Audit context

Continues the audit pattern. The audit has now covered:
- v0.5.48-v0.5.49: OTLP wire-format bugs (schema + encoder).
- v0.5.50: W3C spec compliance (LogRecord trace correlation).
- v0.5.51: memory safety (slab) + algorithm precision (sampler).
- v0.5.52: security (HTTP header injection).

The audit keeps finding real bugs across diverse categories
(wire format, spec, memory, security). Each pass narrows the
gap further.

## Next likely targets

- W3C Baggage parser validation (similar CRLF / format checks).
- Tracestate format validation.
- The tracer's PRNG seeding and ID uniqueness guarantees.
- The arena allocator (if used).
