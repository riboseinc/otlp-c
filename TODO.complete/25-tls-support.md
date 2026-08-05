# TODO 25 — OS-native TLS for HTTPS endpoints — REJECTED

**Status:** WONTFIX (v1.x)
**Priority:** —
**Reason:** violates ADR 0001 (zero non-libc deps).

## Why rejected

TLS libraries (OpenSSL, mbedTLS, GnuTLS, BoringSSL) are all
third-party and ~100 KLOC+ each. OS-native TLS APIs
(Security.framework on macOS, SChannel on Windows) are not part of
libc and vary by platform. Adding any of these violates the load-
bearing embedding constraint that defines the project.

Direct-to-cloud HTTPS is the otelcol sidecar's job — see ADR 0004
(`docs/adr/0004-otlpcol-sidecar-tls.md`) and
`docs/deployment.md`. The library talks plain HTTP to localhost;
the sidecar terminates TLS to the real backend.

## When this could be revisited

If a future major version (1.0+) ships a separate `otlp-c-tls`
package alongside the zero-deps core, that package could bundle
mbedTLS as an optional dependency. The core library stays zero-deps.

That decision is out of scope for the 0.x release line. Close this
TODO until v1.0 planning.
