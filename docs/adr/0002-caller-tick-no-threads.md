# ADR 0002 — Caller-tick, no library threads

## Status

Accepted, 2026-08-05. Codified in `CLAUDE.md` invariant: "no library
threads."

## Context

The classical exporter pattern in OpenTelemetry SDKs is asynchronous
+ thread-pool: the caller emits, the library queues, a worker thread
drains the queue and posts. This is the path `opentelemetry-cpp`,
`opentelemetry-java`, `opentelemetry-rust` etc. take.

For a library that is embedded in kernels, VMs, and firmware,
that pattern is load-bearing *against* the use case:

- **Kernel modules** — kernel threads have a different API than
  `pthread`; you can't `pthread_create` from a kernel module.
- **Firmware** — may have no scheduler.
- **Language VMs** — VM owns the threads; an extra native thread
  breaks GC scans and per-thread state.
- **libc preload** — extra threads in the host process; after
  `fork()`, threads spawned pre-fork don't survive in the child.
- **Embedded with cooperative scheduling** — yield semantics differ.

## Decision

`otlp-c` is **never** concurrent internally. The caller invokes
`otlp_exporter_tick(exp, max_wait_ms)` from a thread it controls.
Cross-thread data flow uses atomics + the Vyukov MPSC queue. No
mutexes, no condvars, no threads.

For an event-loop caller, `tick()` is non-blocking and can be called
on every loop iteration. For a no-loop caller, `tick()` may be called
after every `emit()`. For a thread-less caller, `flush()` syncs
through the calling thread.

## Alternatives considered

1. **Background thread + thread-pool.** Standard in OTel SDKs;
  rejected for the reasons above.
2. **Caller-tick with optional worker thread.** Avoid the "no threads"
  constraint by gating on a build flag. Shipped as `otlp-c-threaded`
  separately? Out of scope; the user can spawn their own thread and
  call `tick()` from it. *Rejected.*
3. **Async-only with no sync option.** Forces every caller to wire
  an event loop, even simple CLI tools. *Rejected.*

## Consequences

- (+) Embeds in any environment.
- (+) Caller controls timing, buffering, and shutdown.
- (-) Caller must remember to call `tick()` periodically. Loud
  documentation + `flush()` convenience + `get_stats()` for stalled-batch
  diagnostics.
- (-) Stats counters are atomic so there is a memory-ordering cost on
  the hot path. (~1 ns on x86, ~5 ns on aarch64.)
