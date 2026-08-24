# Architecture

How the modules fit together. Read this before changing anything
non-trivial.

## The one-paragraph model

Caller code builds telemetry via the public API (`include/otlp-c/`).
Three signals are supported: traces (spans with attributes, events,
links), metrics (counter, gauge, histogram, exponential histogram),
and logs (structured records with trace correlation). Since v0.5.28,
ALL three signals flow through the same async pipeline: each signal
has its own lock-free MPSC queue; `tick()` drains all three by
priority, batches by size and time, encodes as protobuf wire bytes
via schema-driven tables, and POSTs to an OTLP collector over
non-blocking HTTP/1.1. One in-flight HTTP request at a time (shared
across signals). A pluggable sampler decides at span-creation time
whether to record. W3C Trace Context + Baggage propagation is
transport-agnostic via callback-based carriers.

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
│   metric.h       otlp_metric_t — counter/gauge/histogram      │
│   log.h          otlp_log_record_t — structured logs          │
│   sampler.h      otlp_sampler_t — pluggable sampling          │
│   context.h      otlp_context_t — W3C trace propagation        │
│   w3c.h          traceparent header format/parse              │
│   slab.h         otlp_slab_t — fixed-slot memory pool         │
│   allocator.h    otlp_set_allocator — custom alloc hook      │
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
│   │ Span lifecycle,│    │ PRNG, sampler  │                    │
│   │ attrs, events, │    │ consultation,  │                    │
│   │ links, status  │    │ ID generation  │                    │
│   └────────────────┘    └────────────────┘                    │
│                                                               │
│   ┌────────────────┐    ┌────────────────┐                    │
│   │ metric.c       │    │ log.c          │                    │
│   │ Metric lifecycle│   │ Log lifecycle  │                    │
│   └────────────────┘    └────────────────┘                    │
│                                                               │
│   ┌────────────────┐    ┌────────────────┐                    │
│   │ sampler.c      │    │ context.c      │                    │
│   │ Built-in       │    │ Inject/extract │                    │
│   │ samplers       │    │ via carriers   │                    │
│   └────────────────┘    └────────────────┘                    │
│                                                               │
│   ┌────────────────────────────────────────────────┐          │
│   │ exporter.c                                     │          │
│   │ Lock-free MPSC, batch, retry, null-transport.  │          │
│   │ Traces: async emit + tick.                     │          │
│   │ Metrics/Logs: sync flush.                      │          │
│   └────────────────────────────────────────────────┘          │
│                            │                                  │
│                            │ encode + send                    │
│          ┌─────────────────┼─────────────────┐                │
│          ▼                 ▼                 ▼                │
│   ┌──────────────┐ ┌──────────────┐ ┌──────────────┐         │
│   │otlp_messages.│ │otlp_metrics_ │ │otlp_logs_    │         │
│   │c             │ │encoder.c     │ │encoder.c     │         │
│   │Traces encoder│ │Metrics encoder│ │Logs encoder │         │
│   │+ shared      │ │              │ │              │         │
│   │helpers       │ │              │ │              │         │
│   └──────────────┘ └──────────────┘ └──────────────┘         │
│          │                                              │     │
│          ▼                                              │     │
│   ┌──────────────────┐         ┌──────────────────┐       │
│   │ protobuf_encode.c│         │ http_client.c    │       │
│   │ Wire encoder +   │         │ HTTP/1.1 state   │       │
│   │ SBO buffer.      │         │ machine + keep-  │       │
│   │                  │         │ alive.           │       │
│   └──────────────────┘         └──────────────────┘       │
│          │                                  │              │
│   ┌──────────────────┐              ┌──────────────────┐   │
│   │ otlp_schema.h    │              │ platform.c       │   │
│   │ Model-driven     │              │ platform_unix.c  │   │
│   │ field tables.    │              │ platform_win.c   │   │
│   └──────────────────┘              └──────────────────┘   │
│                                                              │
│   ┌──────────────────┐              ┌──────────────────┐   │
│   │ slab.c           │              │ mpsc_queue.c     │   │
│   │ Slab allocator + │              │ Lock-free MPSC   │   │
│   │ global hook.     │              │ ring (Vyukov).   │   │
│   └──────────────────┘              └──────────────────┘   │
│                                                              │
│   ┌──────────────────┐                                     │
│   │ atomic_compat.h  │                                     │
│   │ GCC/Clang: stdatomic│                                 │
│   │ MSVC: intrinsics  │                                   │
│   └──────────────────┘                                     │
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
| `span.c` | Span lifecycle, attributes, events, links, clone | Tracer state, transport |
| `tracer.c` | Tracer state, PRNG, ID generation, sampler consultation | Span contents |
| `metric.c` | Metric lifecycle, record() bucketing | Wire encoding |
| `log.c` | Log record lifecycle | Wire encoding |
| `sampler.c` | Built-in samplers (always_on/off/ratio) | Tracer integration |
| `context.c` | Context inject/extract via carriers | HTTP headers |
| `exporter.c` | 3 MPSC queues (span/metric/log), batch, retry, null-transport, diagnostics | Encoding details |
| `protobuf_decode.c` | Bounds-checked wire reader (PartialSuccess decode, fixed32/64) | Encoding, response policy |
| `exporter_otel.c` | Span batch → HTTP request builder | Queue, retry |
| `otlp_messages.c` | Traces encoder + shared helpers (any_value, resource, scope, attributes) | Metric/log encoding |
| `otlp_metrics_encoder.c` | Metrics encoder (table-driven dispatch) | Traces/logs encoding |
| `otlp_logs_encoder.c` | Logs encoder | Traces/metrics encoding |
| `otlp_schema.h` | Model-driven field tables (single source of truth) | Encoder logic |
| `protobuf_encode.c` | Protobuf wire encoding + SBO buffer (192 B inline — sized so span envelopes and typical attribute KeyValues never touch the heap, v0.5.86) | OTLP schema, transport |
| `http_client.c` | HTTP/1.1 state machine + keep-alive + timeouts | OTLP semantics, response parsing |
| `http_response_parser.c` | HTTP/1.1 response wire format: status line, line-aligned header scan, chunked decode (in place), smuggling rejection, Retry-After | Sockets, clocks, allocation |
| `retry_policy.c` | Retry timing as pure functions: full-jitter draws, exponent clamp, Retry-After floor + cap | Clocks, exporter state |
| `platform.c` / `_unix.c` / `_win.c` | Clocks (nano + ms), non-blocking sockets | Public API |
| `mpsc_queue.c` | Lock-free MPSC ring buffer | Exporter logic |
| `atomic_compat.h` | Atomic operations (GCC/Clang pass-through, MSVC intrinsics) | Lock-free algorithms |
| `slab.c` | Slab allocator + global allocator integration | Memory layout of types |
| `w3c.c` | Traceparent header format/parse | Context propagation |
| `internal_util.c` | Malloc wrappers, string/bytes duplication, attribute free, UTF-8 validator, the one set-attribute engine | Domain logic |

The diagnostics model (v0.5.100) spans `exporter.c`: every
diagnostic is an `otlp_event_t` (the model); the string messages
are DERIVED from it by one formatter, so the structured
(`set_event_logger`) and string (`set_logger`) views cannot
diverge. UTF-8 validation (v0.5.103) lives at the API boundary —
`internal_util`'s validator backs the set-attribute engine (all
six surfaces) and the scalar wire-string setters, so one invalid
value fails its setter (`OTLP_ERR_UTF8`) instead of letting a
Go-based collector reject the whole request. Wire conformance is
enforced by two independent tests: `unit-wire-numbers` pins all
31 schema tables against opentelemetry-proto literals, and
`unit-golden` compares whole payloads against the reference
serialization (see `tests/golden/`).

## Design patterns

### Model-driven encoding (OCP/DRY)

All OTLP field numbers and wire types come from `src/otlp_schema.h`
tables with named enum indices. Encoders reference these tables via
accessor macros:

```c
#define SPAN_F_NAME OTLP_SPAN_FIELDS[OTLP_SPAN_FI_NAME].number
```

Adding a new field to a message: one schema entry + one emit call.
No other file changes. Adding a new AnyValue variant: one encoder
function + one `attr_encoders[]` table entry.

### Table-driven metric dispatch (OCP)

Metric kind → encoder dispatch via `metric_kind_specs[]` table:
```c
static const struct metric_kind_spec metric_kind_specs[] = {
    [OTLP_METRIC_COUNTER]   = { OTLP_METRIC_FI_SUM,        emit_number_data_point },
    [OTLP_METRIC_GAUGE]     = { OTLP_METRIC_FI_GAUGE,      emit_number_data_point },
    [OTLP_METRIC_HISTOGRAM] = { OTLP_METRIC_FI_HISTOGRAM,  emit_histogram_data_point },
    [OTLP_METRIC_EXP_HISTOGRAM] = { OTLP_METRIC_FI_EXP_HISTOGRAM,
                                    emit_exp_histogram_data_point },
};
```

Adding a metric type: one function + one table entry. No switch
to modify.

### Caller-driven I/O (no library threads)

The library never calls `pthread_create` or `_beginthreadex`.
The caller drives I/O by calling `otlp_exporter_tick()` from a
thread it controls. Cross-thread data flow uses atomics only
(via `atomic_compat.h`), no mutexes.

### Lock-free MPSC queue

Vyukov-style bounded ring with per-slot sequence numbers on C11
atomics (or MSVC intrinsics via `atomic_compat.h`). Multi-producer
safe; single-consumer (the tick caller).

## Invariants

1. **Opaque types**. Every public type is `typedef struct foo foo;`
   in the header with the struct definition in the `.c` file.
2. **Zero non-libc deps**. No third-party headers or link deps.
3. **No C++**. C99 style; C11 for `<stdatomic.h>`.
4. **Error code on every fallible function**. Returns `otlp_status_t`.
5. **Lock-free**. No mutexes. Cross-thread via atomics + MPSC queue.
6. **No library threads**. Caller drives I/O via `tick()`.

## Testing strategy

- **27 property tests** (`tests/property/`): QuickCheck-style, deterministic
  (null_transport mode). Covers varint, encoder, messages, URL parser,
  span lifecycle, attribute round-trip, W3C, metrics, logs,
  events/links/context, sampler, slab, keepalive, exporter batching,
  flush.
- **Unit tests** (`tests/`): smoke, HTTP echo, exporter echo, retry.
- **Integration test** (`tests/integration/`): end-to-end against otelcol + Jaeger.
- **Concurrency stress** (`tests/test_concurrency_stress.c`): 8 threads × 200 spans.

## See also

- [CLAUDE.md](../CLAUDE.md) — conventions and invariants.
- [docs/otlp-spec.md](otlp-spec.md) — the protocol reference.
- [docs/roadmap.md](roadmap.md) — phase-by-phase implementation plan.
- [docs/quickstart.md](quickstart.md) — getting started with all signals.
