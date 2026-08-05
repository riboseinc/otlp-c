# ADR 0003 — Vyukov bounded MPSC queue

## Status

Accepted, 2026-08-05. Implementation in `src/mpsc_queue.{h,c}`.

## Context

The exporter's cross-thread data flow is `emit` (any thread) → queue
→ `tick()` (single consumer). That's an MPSC pattern. We need to
pick a queue design.

Constraints:
- Lock-free (per ADR 0002 — no mutexes).
- Bounded (memory safety; infinite queues leak).
- Predictable memory access pattern (works for TSan).
- Make-no-system-calls (futex is forbidden — we're async event loops).

## Decision

Vyukov's bounded MPMC ring with per-slot sequence numbers. Adapted
to MPSC — we use only one consumer (the tick thread) so the
sequence-number consumer logic is straightforward.

Slot layout: `void*` payload + `_Atomic uint64_t seq`. Producer
threads CAS on the slot's sequence; the consumer walks slots in
order from its `tail` index.

Key invariant: a producer's `seq` value transitions `h+1 → h+1+cap
→ h+1+cap+1` (claim → publish) and the consumer's release transitions
`h+1+cap+1 → h+1+cap+2` (free). CAS on `seq` synchronizes the slot
write.

## Alternatives considered

1. **Linked-list MPSC (Vyukov unbounded).** Per-item malloc. O(1)
  amortized but unbounded memory. *Rejected under "bounded" constraint.*
2. **Per-thread SPSC + consumer round-robin.** Requires producer
  identity tracking. Two-mallocs per emit. *Rejected for complexity.*
3. **Mutex-protected `otlp_span_t*` list.** Violates ADR 0002.
  *Rejected.*

## Consequences

- (+) Lock-free. Passes TSan (verified in TODO 12 stress test).
- (+) Bounded → caller gets `OTLP_ERR_BUFFER_FULL` on overflow.
- (-) Per-slot atomic CAS — ~5 ns on x86, ~20 ns on aarch64. Slower
  than a mutex on a single producer but parallelism scales.
- (+) Memory layout is contiguous (cache-friendly).
- (-) Implementation is subtle (Vyukov sequence numbers). Property
  test in `tests/property/test_property_mpsc.c` covers correctness.
