# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What otlp-c is

`otlp-c` is a pure-C99 client library for the OpenTelemetry Protocol (OTLP/HTTP). It emits trace spans (and, in future, metrics and logs) over HTTP/1.1 to an OpenTelemetry collector.

The hard constraint that defines this project: **zero non-libc dependencies**. No C++ runtime. No vendored protobuf. No TLS library (defer TLS to a local `otelcol` sidecar). No async runtime. The library must build with a C99 compiler and link cleanly into any C application, including kernel modules, embedded firmware, language runtimes, and libc-preloaded tracers.

## The non-negotiable invariants

These are not stylistic preferences; they are load-bearing. Breaking any of them defeats the project's reason for existing.

1. **Pure C99.** No C++ files. No `extern "C"` wrappers around C++ implementations. If you need atomic operations, use C11 `<stdatomic.h>` or platform intrinsics — never C++ atomics.
2. **Zero non-libc runtime dependencies.** No protobuf, no gRPC, no libcurl, no OpenSSL, no zlib. The encoder is hand-rolled for the ~6 OTLP message types we need. See `docs/otlp-spec.md` for the schema.
3. **Apache 2.0 only.** Every line committed must be Apache-2.0 compatible. Don't introduce BSD-only or GPL code. This matters for the eventual CNCF donation path.
4. **No telemetry from otlp-c itself.** The library does not phone home. No version checks. No usage reporting.
5. **Public API stability within a major version.** Once 1.0.0 ships, no breaking API changes within the 1.x line. New features = new functions or new opt-in structs. See `docs/roadmap.md` for the version policy.
6. **Stubbed-default builds.** `cmake -B build && cmake --build build` must succeed and link a working stub library. Real implementations live behind feature flags or in src subdirectories.

## Build

CMake 3.20+, Ninja recommended.

```sh
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Useful CMake options:

- `OTLP_C_BUILD_TESTS` (default OFF) — build the test binaries in `tests/`.
- `OTLP_C_BUILD_EXAMPLES` (default OFF) — build the demo programs in `examples/`.
- `OTLP_C_ENABLE_ASAN` (default OFF) — AddressSanitizer.
- `OTLP_C_ENABLE_UBSAN` (default OFF) — UndefinedBehaviorSanitizer.
- `BUILD_SHARED_LIBS` (default OFF) — build as shared library instead of static.

vcpkg manifest mode is supported; the manifest currently declares no required dependencies (by design).

## Running tests

CTest drives the test pyramid. Labels: `unit`, `property`, `integration`, `smoke`.

```sh
ctest --test-dir build                 # all
ctest --test-dir build -L unit         # unit only
ctest --test-dir build -L property     # property-based
ctest --test-dir build -L integration  # needs Docker for otelcol
```

Property tests take an env var override for the iteration count: `OTLP_C_PROPERTY_ITERS=100000 ctest ...`. Reproduce a specific failure with `OTLP_C_PROPERTY_SEED=<seed>`.

## Code style

- **`.clang-format`** is Mozilla-based. Run `clang-format -i` on changed files before committing.
- **checkpatch** (Linux kernel style with project-specific ignores) runs in CI. See `ci/checkpatch.sh`.
- **Common gotchas**:
  - Block comments: `*/` on its own line for multi-line comments.
  - Blank line after declarations before statements.
  - No `do { ... } while (0)` macros with control flow inside (use static inline functions instead).
  - No multi-statement macros with side effects.

## Architecture

See [docs/architecture.md](docs/architecture.md) for the full layered view. Summary:

```
caller code  ───►  public API (include/otlp-c/)  ───►  implementation (src/)
                                                                │
                                                                ▼
                                              ┌─────────────────┴──────────────────┐
                                              │                                    │
                                     protobuf encoder                  HTTP/1.1 client
                                     (src/protobuf_encode.c)            (src/http_client.c)
                                              │                                    │
                                              └─────────────────┬──────────────────┘
                                                                │
                                                                ▼
                                                          OTLP/HTTP wire
                                                                │
                                                                ▼
                                                          otelcol (external)
```

Module responsibilities are MECE: each file owns exactly one concern. The protobuf encoder doesn't know about HTTP. The HTTP client doesn't know about OTLP. The exporter batches spans and calls both.

## Don'ts

- **Don't add a third-party dependency.** If you reach for libcurl or protobuf-c, you're solving the wrong problem. Hand-roll the surface you need in `src/`.
- **Don't use C++.** C99 only. C11 is acceptable if `<stdatomic.h>` is needed; document why.
- **Don't change the public API without bumping the major version.** (Within the 0.x line, the API is allowed to break; document changes in CHANGELOG.)
- **Don't add telemetry-from-the-library.** The library emits OTLP about the user's code, never about itself.
- **Don't reach into another module's static state.** Use accessor functions; declare opaque types in headers.
- **Don't hand-edit vendored .proto definitions.** Hand-roll the message structs in `src/otlp_messages.h` and keep them in sync with the spec at `docs/otlp-spec.md`.

## Key files to know

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | All build switches, feature probes |
| `vcpkg.json` | Dependency manifest (currently empty by design) |
| `include/otlp-c/otlp.h` | Umbrella public header |
| `include/otlp-c/exporter.h` | The main entry point most callers use |
| `include/otlp-c/span.h` | Span type — the unit of telemetry |
| `include/otlp-c/tracer.h` | Tracer — owns span creation |
| `src/protobuf_encode.c` | Protobuf wire encoder (hand-rolled) |
| `src/http_client.c` | HTTP/1.1 client (raw socket) |
| `src/exporter_otel.c` | OTLP/HTTP exporter — batches + flushes |
| `tests/property/` | Property-based tests (QuickCheck-style) |
| `tests/integration/` | End-to-end against a local otelcol |
| `docs/otlp-spec.md` | The OTLP protocol reference |
| `docs/architecture.md` | The layered design |
| `docs/roadmap.md` | Phase-by-phase implementation plan |
| `ci/checkpatch.sh` | checkpatch driver + ignore list |
| `.github/workflows/build.yml` | Multi-platform CI |

## The OTLP protocol

OTLP/HTTP is well-specified by [opentelemetry.io](https://opentelemetry.io/docs/specs/otlp/). The full reference (with field numbers) is in [docs/otlp-spec.md](docs/otlp-spec.md).

What you need to know day-to-day:

- **Endpoint**: `POST http://<host>:4318/v1/traces` for traces. Same pattern for metrics (`/v1/metrics`) and logs (`/v1/logs`).
- **Body**: a Protobuf-encoded `ExportTraceServiceRequest` message.
- **Content-Type**: `application/x-protobuf`.
- **Response**: 200 OK on success; 4xx / 5xx with a structured error message on failure.
- **Schema**: lives in [opentelemetry-proto](https://github.com/open-telemetry/opentelemetry-proto), Apache 2.0. We hand-roll the message structs (see `src/otlp_messages.h`).

## For the implementing agent

When you pick up this repository, work through [docs/roadmap.md](docs/roadmap.md) phase by phase:

1. **Phase 1**: Protobuf wire encoder (`src/protobuf_encode.c`). Property-test it.
2. **Phase 2**: Message struct definitions (`src/otlp_messages.h`).
3. **Phase 3**: HTTP/1.1 client (`src/http_client.c`).
4. **Phase 4**: Span builder (`src/span.c`).
5. **Phase 5**: Exporter with batching (`src/exporter.c`, `src/exporter_otel.c`).
6. **Phase 6**: Integration test against a real `otelcol`.
7. **Phase 7**: Documentation, examples polish.
8. **Phase 8**: Tag 0.1.0.

Each phase has acceptance criteria in the roadmap. Don't skip ahead — later phases depend on earlier ones being solid.

The stubs in `src/*.c` are placeholders that compile and link but return `OTLP_ERR_NOT_IMPLEMENTED`. Replace each stub with its real implementation in order. Run `ctest --test-dir build -L property` after every phase; the property tests catch regressions in the encoder immediately.

## Conventions

- **Opaque types**: every public type is `typedef struct foo foo;` in the header, with the struct definition in the .c file. No public struct members.
- **Error codes**: every public function returns `otlp_status_t`. `OTLP_OK` (0) is success; negative values are errors. See `include/otlp-c/status.h`.
- **Ownership**: functions that return heap-allocated pointers have `_create` / `_free` pairs. The caller owns the pointer between them. Document ownership in the docstring.
- **Threads**: the library is thread-safe at the exporter level (the exporter holds a mutex around batch emission). Span construction is single-threaded by design — each thread builds its own span.
- **Memory**: use the platform's `malloc`/`free`. Custom allocators are a P1 feature; defer.

## CI

`.github/workflows/build.yml` runs:

- Linux x86_64, macOS arm64, macOS x86_64, Windows x64 (matrix).
- Build + unit tests + property tests.
- vcpkg manifest mode.

`.github/workflows/checkpatch.yml` runs checkpatch on every PR.

PRs require green CI before merge. Rebase-merge is the canonical merge style.

## Versioning

Semantic versioning (semver.org). Within the 0.x line, the API may break between minor versions — document changes in `CHANGELOG.md`. At 1.0.0, the API freezes for the 1.x line.

The version is defined in `include/otlp-c/version.h` and bumped in lockstep with `CMakeLists.txt` and `vcpkg.json`.

## See also

- [README.md](README.md) — the pitch.
- [docs/otlp-spec.md](docs/otlp-spec.md) — the protocol reference.
- [docs/architecture.md](docs/architecture.md) — the design.
- [docs/roadmap.md](docs/roadmap.md) — the plan.
- [SECURITY.md](SECURITY.md) — disclosure handling.
- [CONTRIBUTING.md](CONTRIBUTING.md) — contribution workflow.
