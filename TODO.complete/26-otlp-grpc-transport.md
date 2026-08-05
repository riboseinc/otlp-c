# TODO 26 — OTLP/gRPC transport option — REJECTED

**Status:** WONTFIX (v1.x)
**Priority:** —
**Reason:** requires HTTP/2 (which itself requires TLS) — both
violate ADR 0001.

## Why rejected

gRPC requires HTTP/2. HTTP/2 in production requires TLS (h2c is
rarely supported by collectors). TLS is rejected in TODO 25. So
gRPC transitively violates the same invariants.

A sidecar collector (otelcol) accepts OTLP/HTTP on :4318 and can
forward to OTLP/gRPC on :4317 internally — that's the standard
deployment topology. The library's job is plain HTTP to localhost.

## When this could be revisited

Same as TODO 25: a future `otlp-c-grpc` package could exist
alongside the zero-deps core, but only if HTTP/2 + TLS deps are
acceptable. Out of scope for the 0.x release line.
