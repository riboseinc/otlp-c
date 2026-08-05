# ADR 0001 — Zero non-libc dependencies

## Status

Accepted, 2026-08-05. Codified in `CLAUDE.md` invariant: "zero
non-libc deps."

## Context

`otlp-c` is a C library for embedding in environments where libc
compliance is non-negotiable:

- **Kernel modules** — no C++ runtime, no userspace libraries.
- **Embedded firmware** — limited flash; static linking mandatory.
- **Language VMs** (CPython, Ruby MRI, Lua) — adding a TCP C++ dep
  to the host process is unacceptable.
- **libc-preloaded tracers** — we malloc inside another program's
  address space; anything that pulls in signals or threads breaks
  the host.

The natural temptation is to use a mature protobuf encoder
(`protobuf-c`), an HTTP client (`libcurl`), and a TLS library
(`OpenSSL` or `mbedTLS`). All would be ~100 KLOC of additional code,
require careful ABI/version compatibility, and violate the load-bearing
embedding constraint.

## Decision

`otlp-c` depends only on the C standard library (`libc`). The
protobuf encoder is hand-rolled for the ~6 OTLP message types in
scope. The HTTP client is a state machine over a non-blocking socket
(no `libcurl`). TLS is delegated to an external `otelcol` sidecar
process, not in the library.

## Alternatives considered

1. **Vendor protobuf-c, libcurl, OpenSSL as submodules.** Adds
   ~100 KLOC and ~5 MB of installed overhead. Prohibits embedding
   use cases. *Rejected.*
2. **Allow optional TLS via a runtime feature flag.** Tempting, but
   no production TLS stack is meaningful at <100 KB. *Rejected.*
3. **Depend on a POSIX extension (pthreads, getaddrinfo) on top of
   libc.** We accept this — POSIX is the de-facto standard on Unix
   and Windows implements equivalents (`Ws2_32`). Effective floor is
   "POSIX-ish," not "C-standard."

## Consequences

- (+) Library is ≤ 10 KLOC, can be embedded everywhere.
- (+) No transitive dependency hell.
- (-) Adding new OTLP fields requires hand-coding the encoder entry.
  Mitigated by the schema-table-driven design in `src/otlp_schema.h`.
- (-) TLS termination is operational complexity (run a sidecar).
  Documented in `docs/deployment.md`.
