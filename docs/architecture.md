# Architecture

How the modules fit together. Read this before changing anything
non-trivial.

## The one-paragraph model

Caller code builds spans via the public API (`include/otlp-c/`).
Each span carries a name, timestamps, attributes, status, and
optional events/links. The exporter batches spans by size and
time, then encodes each batch as a Protobuf
`ExportTraceServiceRequest` and POSTs it to an OTLP collector
over HTTP/1.1. Retry with exponential backoff handles transient
failures.

## Layered view

```
┌──────────────────────────────────────────────────────────────┐
│ Caller code (the application)                                 │
└──────────────────────────────────────────────────────────────┘
                            │
                            │ public API
                            ▼
┌──────────────────────────────────────────────────────────────┐
│ Public API  (include/otlp-c/)                                 │
│   otlp.h         umbrella header                              │
│   span.h         otlp_span_t construction + lifetime          │
│   tracer.h       otlp_tracer_t — owns span creation           │
│   exporter.h     otlp_exporter_t — batches + flushes          │
│   status.h       error codes                                  │
│   version.h      version constants                            │
└──────────────────────────────────────────────────────────────┘
                            │
                            │ implementation (opaque types)
                            ▼
┌──────────────────────────────────────────────────────────────┐
│ Implementation  (src/)                                        │
│                                                               │
│   ┌────────────────┐    ┌────────────────┐                    │
│   │ span.c         │    │ tracer.c       │                    │
│   │ Span lifetime, │    │ Tracer owns    │                    │
│   │ attributes,    │    │ span context.  │                    │
│   │ events, status │    │                │                    │
│   └────────────────┘    └────────────────┘                    │
│                                                               │
│   ┌────────────────────────────────────────────────┐          │
│   │ exporter.c                                     │          │
│   │ Batch buffer, retry policy, background flush.  │          │
│   └────────────────────────────────────────────────┘          │
│                            │                                  │
│                            │ encode + send                    │
│                            ▼                                  │
│   ┌────────────────┐    ┌────────────────┐                    │
│   │ exporter_otel.c│───▶│ http_client.c  │                    │
│   │ OTLP/HTTP      │    │ HTTP/1.1 over  │                    │
│   │ encoder + POST │    │ raw socket.    │                    │
│   └────────────────┘    └────────────────┘                    │
│          │                                  │                 │
│          ▼                                  ▼                 │
│   ┌──────────────────┐              ┌──────────────────┐      │
│   │ protobuf_encode.c│              │ platform.c       │      │
│   │ Hand-rolled      │              │ Sockets, time,   │      │
│   │ wire encoder.    │              │ mutex (POSIX +   │      │
│   │                  │              │ Win32).          │      │
│   └──────────────────┘              └──────────────────┘      │
└──────────────────────────────────────────────────────────────┘
                            │
                            ▼
                  OTLP/HTTP wire format
                            │
                            ▼
                    otelcol (external)
```

## Module responsibilities (MECE)

| Module | Owns | Doesn't own |
|---|---|---|
| `span.c` | Span construction, attributes, status, events | Tracer state, transport |
| `tracer.c` | Tracer state, span ID generation, parent linking | Span contents |
| `exporter.c` | Batch buffer, flush scheduling, retry policy | Encoding, transport |
| `exporter_otel.c` | OTLP/HTTP request encoding, response handling | Buffer management |
| `protobuf_encode.c` | Protobuf wire encoding (varint, length-delimited, fixed) | OTLP schema, transport |
| `http_client.c` | HTTP/1.1 request/response, connection handling | OTLP semantics |
| `platform.c` | Cross-platform wrappers (sockets, time, mutex) | Public API |
| `otlp_messages.h` | C struct definitions matching the OTLP .proto | Encoder logic |

Adding a feature: pick the right module; don't spread it across files. Adding a new signal (metrics, logs): add a sibling to `exporter_otel.c` (e.g. `exporter_otel_metrics.c`) and a sibling message struct file.

## Invariants

1. **Opaque types**. Every public type is `typedef struct foo foo;`
   in the header with the struct definition in the `.c` file. Callers
   never reach into struct fields.
2. **Zero non-libc deps**. No third-party headers in `#include`. No
   third-party link deps at runtime.
3. **No C++**. C99 (C11 only where `<stdatomic.h>` is needed).
4. **Error code on every public function**. Functions that can fail
   return `otlp_status_t`. Functions that can't (e.g. getters on a
   non-NULL span) return the value directly.
5. **Thread-safety at the exporter only**. The exporter holds a
   mutex around batch emission. Span construction is single-threaded
   by design.
6. **No malloc in hot paths unless necessary**. The batch buffer is
   preallocated; span construction uses a per-span arena (future).

## Public API conventions

### Naming

- Types: `otlp_<noun>_t` (`otlp_span_t`, `otlp_exporter_t`).
- Functions: `otlp_<noun>_<verb>` (`otlp_span_create`, `otlp_exporter_emit`).
- Macros and constants: `OTLP_<NAME>` (`OTLP_OK`, `OTLP_VERSION_MAJOR`).
- Enums: `otlp_<noun>_tag` for the type, `OTLP_<NOUN>_<KIND>` for the values.

### Ownership

- `_create` returns a heap pointer; caller owns it.
- `_free` releases the pointer.
- Anything else does not transfer ownership.

### Error handling

- Return `otlp_status_t` from every fallible function.
- `OTLP_OK` is `0`. Errors are negative.
- Document the error codes each function can return.

### Threads

- `_create`, `_free`: caller's responsibility to serialize.
- `_emit`, `_flush`, `_shutdown`: thread-safe (exporter holds a mutex).
- Span mutation (`set_attribute`, etc.): single-threaded per span.

## Build-time decisions

### Static vs shared

Default: static. `BUILD_SHARED_LIBS=ON` builds shared.

The static library is ~50 KB on Linux/x86-64. The shared library
is ~80 KB (exports + position-independent code).

### Symbols

Public symbols are exported with platform-appropriate visibility
attributes (`__attribute__((visibility("default")))` on Linux/ELF,
`__declspec(dllexport)` on Windows). Internal symbols are hidden.

### Position independence

Always compiled PIC so the static library can be linked into a
shared library.

## Testing strategy

- **Unit tests** (`tests/unit/`): per-module function tests. Fast.
- **Property tests** (`tests/property/`): QuickCheck-style. Run
  10K iterations per property. See `tests/property/property_harness.h`.
- **Integration tests** (`tests/integration/`): end-to-end against
  a local `otelcol` Docker container. Needs Docker.
- **Smoke tests** (`tests/smoke/`): minimal "library loads and runs"
  per platform.

## Performance budget

Single-digit microseconds to construct and free a span. <1 ms to
encode and POST a 512-span batch (excluding network). <5% wall-
clock overhead on a typical instrumented target.

These are the budgets that drive the design. The roadmap's perf-
regression test will enforce them.

## See also

- [CLAUDE.md](../CLAUDE.md) — conventions and invariants.
- [docs/otlp-spec.md](otlp-spec.md) — the protocol reference.
- [docs/roadmap.md](roadmap.md) — phase-by-phase implementation plan.
