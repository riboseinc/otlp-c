# otlp-c

A pure-C library for emitting OpenTelemetry telemetry via the OpenTelemetry Protocol (OTLP/HTTP).

[![Build](https://github.com/riboseinc/otlp-c/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/riboseinc/otlp-c/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

## What this is

`otlp-c` is a pure-C99 client for the OpenTelemetry Protocol. It
produces OTLP/HTTP payloads for all three signals — traces, metrics,
and logs — posts them to an OTLP collector (such as
[otelcol](https://github.com/open-telemetry/opentelemetry-collector)),
and lets any C application emit OTel-compliant telemetry without
dragging in a C++ runtime.

The official OpenTelemetry C++ SDK ([opentelemetry-cpp](https://github.com/open-telemetry/opentelemetry-cpp)) is excellent — but it's C++. That closes the door for C-only projects: kernel modules, embedded firmware, language runtimes, libc-preloaded tracing tools, and any project that needs to stay buildable with just a C compiler.

`otlp-c` fills that gap. Pure C99. Zero non-libc dependencies. Apache 2.0 (compatible with [CNCF](https://www.cncf.io/) governance, suitable for future donation to the OpenTelemetry project).

## Status

**0.5.35.** Full OTLP/HTTP client for all three signals (traces,
metrics, logs). Features: hand-rolled protobuf encoder with
schema-driven field tables, lock-free MPSC queue + caller-tick
exporter, non-blocking HTTP/1.1 client with keep-alive, W3C Trace
Context propagation, sampler interface (always_on / always_off /
trace_id_ratio_based), slab allocator, span events/links/trace_state,
context propagation with tracestate, and more.

CI'd platforms: Linux x86_64/ARM64, macOS Intel/ARM64, Windows
x64/ARM64, FreeBSD 14.2 (best-effort). Expected to work on any
POSIX platform with a C11 compiler and a socket stack.

The API surface is unstable until 1.0.0. Within the 0.x line, minor
versions may break the API (documented in CHANGELOG).

## Why

OpenTelemetry ships SDKs in eleven languages. Two of them are C-callable:

| Option | Pure-C API? | Pure-C implementation? | Notes |
|---|---|---|---|
| OpenTelemetry C++ SDK | via wrappers | no | requires C++ compiler + runtime |
| HAProxy `opentelemetry-c-wrapper` | yes | no (wraps C++) | requires C++ compiler + runtime |
| dorsal-lab `opentelemetry-c` | yes | no (wraps C++) | smaller community |
| **`otlp-c` (this project)** | **yes** | **yes** | **C99 only, zero non-libc deps** |

For projects where a C++ runtime dependency is unacceptable, this is the only path to native OTLP support.

## Use cases

- **Kernel modules** — no C++ available.
- **Embedded firmware** — no C++ runtime.
- **Language runtimes** (CPython, Ruby MRI, Lua) — don't want a C++ dep in the host process.
- **Libc-preloaded tracers** (e.g. [retrace](https://github.com/riboseinc/retrace)) — must stay buildable with a C compiler only.
- **Static binaries** — no C++ standard library to link.
- **Security-critical code** — minimal attack surface; no protobuf runtime, no async runtime, no template metaprogramming.

## Features

- **Traces**: spans with attributes, events, links, trace_state,
  status, sampling. Async emit + caller-tick batching with
  exponential backoff retry (full jitter).
- **Metrics**: counter, gauge, histogram, exponential histogram.
  Async emit (`emit_metric_move`, v0.5.28) + synchronous flush
  fallback. Configurable aggregation temporality + is_monotonic.
- **Logs**: structured log records with severity, body, trace
  correlation. Async emit (`emit_log_move`, v0.5.28) + synchronous
  flush fallback.
- **Context propagation**: W3C Trace Context (traceparent +
  tracestate) + W3C Baggage via callback-based carrier abstraction.
- **Diagnostics, two views of one model**: the structured
  `set_event_logger` callback delivers every diagnostic as an
  `otlp_event_t` (event code, signal, counts, drop reason,
  retry timing) — no string parsing; the optional `set_logger`
  callback receives the message derived from that same event.
- **Sampler**: pluggable vtable with always_on, always_off, and
  deterministic trace_id_ratio_based built-ins.
- **Attributes**: the full OTLP AnyValue set — string, bool,
  int64, double, bytes, and composite ArrayValue / KeyValueList
  (via the public `otlp_value_t` type) — on spans, events, links,
  metrics, and log records. Map semantics: re-setting a key
  replaces its value. Storage is grow-on-demand: objects pay for
  the attributes they carry, not fixed-cap arrays.
- **Resource attributes**: the full `otlp_value_t` model (all
  AnyValue types, v0.5.92) on every batch's Resource; map
  semantics (last write wins) resolved at create time.
- **Hardened HTTP client**: chunked-response decoding (RFC 7230),
  request-smuggling rejection, version-aware keep-alive, and an
  I/O inactivity deadline across connect/send/read.
- **W3C-spec-exact propagation**: traceparent version rules,
  printable-only tracestate/baggage, big-endian sampler prefix.
- **Server-response aware**: Retry-After honored on throttled
  responses (clamped by your backoff cap); collector
  PartialSuccess — server-side data loss reported on a 200 —
  surfaced via diagnostics and per-signal `rejected_*` stats.
- **UTF-8 validated at the boundary**: every wire string is
  checked at the setter; one invalid value fails itself
  (`OTLP_ERR_UTF8`) instead of letting a collector reject the
  whole request.
- **Slab allocator**: fixed-slot memory pool with malloc fallback.
  Installable as the process-wide allocator (any slot size —
  realloc is arena-aware).
- **Zero dependencies**: no protobuf-c, no libcurl, no OpenSSL,
  no C++ runtime. Hand-rolled protobuf encoder + HTTP/1.1 client.
- **No library threads**: caller-driven I/O. The library never
  calls `pthread_create` or takes a mutex. Embeddable in kernel
  modules, firmware, language VMs.
- **Cross-platform**: Linux, macOS, Windows, FreeBSD, Alpine.
  C11 compiler required (`<stdatomic.h>`).

## Build

CMake + Ninja (recommended). vcpkg manifest mode is supported but currently pulls in no dependencies (the whole point is zero non-libc deps).

```sh
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### vcpkg

Use `otlp-c` from vcpkg:

```sh
# In your project's vcpkg.json:
{
  "dependencies": ["otlp-c"]
}
```

Or build it from this repo:

```sh
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
```

## Quick start

`otlp-c` is **caller-tick**: the library never spawns threads, so it
embeds cleanly into event loops (libuv, epoll, IOCP), game loops,
language VMs, and libc-preloaded tracers. The caller drives I/O by
calling `otlp_exporter_tick()` from a thread it controls.

```c
#include <otlp-c/otlp.h>

int main(void) {
    /* Defaults: endpoint=http://localhost:4318/v1/traces,
     * batch_size=512, batch_ms=100, retries=5. */
    otlp_exporter_opts_t opts = {
        .service_name = "my-service",
    };
    otlp_exporter_t *exp    = otlp_exporter_create(&opts);
    otlp_tracer_t   *tracer = otlp_tracer_create(
        "my-service", "my-app", "1.0.0");

    otlp_span_t *span = otlp_tracer_start_span(tracer, "do-work");
    otlp_span_set_attribute_string(span, "user.id", "alice");
    otlp_span_mark_end(span);

    otlp_exporter_emit(exp, span);     /* any thread, lock-free */
    otlp_span_free(span);

    /* Drive the exporter until drained. flush() loops tick()
     * internally; for hot paths, call tick() from your event loop
     * or periodic timer instead. */
    otlp_exporter_flush(exp);

    otlp_tracer_free(tracer);
    otlp_exporter_free(exp);
    return 0;
}
```

**HTTPS:** the library talks plain HTTP to localhost; an
[otelcol](https://github.com/open-telemetry/opentelemetry-collector)
sidecar terminates TLS to the real backend. This is the standard
production deployment and the only model compatible with the
zero-deps invariant. See [docs/deployment.md](docs/deployment.md).

Working examples: [minimal](examples/minimal.c) (full API
surface, runs standalone), [multithread](examples/multithread.c)
(N worker threads, one tick thread), and
[event_loop_integration](examples/event_loop_integration.c)
(poll()-driven main loop via `otlp_exporter_poll_fds`).

## Documentation

- [CLAUDE.md](CLAUDE.md) — project conventions, invariants, build/test commands. Read this first if you're contributing.
- [docs/otlp-spec.md](docs/otlp-spec.md) — the OTLP/HTTP protocol reference, with the message types `otlp-c` implements.
- [docs/architecture.md](docs/architecture.md) — the layered design, module responsibilities.
- [docs/roadmap.md](docs/roadmap.md) — phase-by-phase implementation plan.
- [include/otlp-c/](include/otlp-c/) — the public API headers (the contract).

## Compatibility

**Operating systems**: Linux, macOS, FreeBSD, Windows are covered
by CI. Any POSIX-compliant platform (OpenBSD, NetBSD, Illumos,
etc.) is expected to work via the POSIX `platform_unix.c` path but
is not continuously tested.
**Architectures**: x86_64, aarch64, armv7, riscv64. Anything with a working C99 compiler and a socket stack should work.

The wire format is platform-independent (Protobuf over HTTP/1.1). The transport layer (sockets, threads, time) uses POSIX on Unix-like systems and Win32 on Windows; both paths live behind the same public API.

## License

Apache License 2.0. See [LICENSE](LICENSE).

The OTel proto schema reference (in [docs/otlp-spec.md](docs/otlp-spec.md)) is drawn from the [opentelemetry-proto](https://github.com/open-telemetry/opentelemetry-proto) repository, also Apache 2.0.

## Contributing

PRs welcome. Read [CLAUDE.md](CLAUDE.md) and [docs/roadmap.md](docs/roadmap.md) first; pick a phase that isn't done yet; open a draft PR early.

For security disclosures, see [SECURITY.md](SECURITY.md) — do not open public issues for vulnerabilities.

## Maintainership

Maintained by [Ribose, Inc.](https://www.ribose.com). Long-term goal: donate to the CNCF / OpenTelemetry project once the API stabilizes and adoption warrants it. Apache 2.0 (not BSD-2) was chosen specifically to ease that path.
