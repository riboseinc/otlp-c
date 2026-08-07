# otlp-c

A pure-C library for emitting OpenTelemetry telemetry via the OpenTelemetry Protocol (OTLP/HTTP).

[![Build](https://github.com/riboseinc/otlp-c/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/riboseinc/otlp-c/actions/workflows/build.yml)
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

**0.5.10.** Full OTLP/HTTP client for all three signals (traces,
metrics, logs). Features: hand-rolled protobuf encoder with
schema-driven field tables, lock-free MPSC queue + caller-tick
exporter, non-blocking HTTP/1.1 client with keep-alive, W3C Trace
Context propagation, sampler interface (always_on / always_off /
trace_id_ratio_based), slab allocator, span events/links/trace_state,
context propagation with tracestate, and more.

Supported platforms: Linux x86_64/ARM64, macOS Intel/ARM64,
Windows x64/ARM64, FreeBSD, Alpine (musl).

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
  exponential backoff retry.
- **Metrics**: counter, gauge, histogram, exponential histogram.
  Synchronous flush to `/v1/metrics`.
- **Logs**: structured log records with severity, body, trace
  correlation. Synchronous flush to `/v1/logs`.
- **Context propagation**: W3C Trace Context (traceparent +
  tracestate) via callback-based carrier abstraction.
- **Sampler**: pluggable vtable with always_on, always_off, and
  deterministic trace_id_ratio_based built-ins.
- **Slab allocator**: fixed-slot memory pool with malloc fallback.
  Installable as the process-wide allocator.
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

See [examples/minimal.c](examples/minimal.c) for a working example.

## Documentation

- [CLAUDE.md](CLAUDE.md) — project conventions, invariants, build/test commands. Read this first if you're contributing.
- [docs/otlp-spec.md](docs/otlp-spec.md) — the OTLP/HTTP protocol reference, with the message types `otlp-c` implements.
- [docs/architecture.md](docs/architecture.md) — the layered design, module responsibilities.
- [docs/roadmap.md](docs/roadmap.md) — phase-by-phase implementation plan.
- [include/otlp-c/](include/otlp-c/) — the public API headers (the contract).

## Compatibility

**Operating systems**: Linux, macOS, FreeBSD, OpenBSD, NetBSD, Windows.
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
