# ADR 0004 — otlpcol sidecar for TLS

## Status

Accepted, 2026-08-05. Documented in `docs/deployment.md`.

## Context

Real OTLP collectors run on TLS. The OTLP spec requires HTTPS for
production. The library is zero-deps. Adding TLS in the library
breaks the zero-deps invariant.

## Decision

The library emits plain HTTP to `localhost:4318`. A local `otelcol`
(or equivalent — Envoy, in-process collector in another VM, etc.)
terminates TLS and forwards to the real backend. This is the standard
sidecar pattern.

## Alternatives considered

1. **Bundle OpenSSL/mbedTLS.** Breaks zero-deps invariant. *Rejected.*
2. **Add a separate `otlp-c-tls` package.** Violates the "one-package"
  goal; doubles the CI surface. *Rejected for v0.x.*
3. **Send plain HTTP to anywhere, including the public internet.**
  Acceptable for development; unsafe for production. *Rejected as
  the default — opt-in via the sidecar.*

## Consequences

- (+) Library stays zero-deps.
- (+) Sidecar handles TLS, auth, queueing, retry, fan-out — every
  concern that's surprising in production.
- (-) Operational complexity: users must run an `otelcol` sidecar.
  Documented in `docs/deployment.md` with concrete manifests for
  Kubernetes, systemd, and bare-metal.
- (-) Adds a network hop. For users running everything on one host
  that's negligible.

## When this changes

If a future major version (1.0+) gains a heavy deployment-option
profile, a separate `otlp-c-tls` package with mbedTLS could exist
alongside. The core library stays zero-deps.
