# TODO 42 — Memory pool for spans (slab allocator)

**Status:** Pending
**Priority:** P2
**Branch:** future (v0.3+)

## Goal

A trace-emitting loop in a hot path (libc preload, per-syscall tracer)
allocates one span per emission. With hundreds of spans per second, the
malloc/free traffic dominates. A small slab allocator (per-thread or
per-exporter) reuses span memory.

## Acceptance criteria

- [ ] `struct otlp_slab` with fixed-size slot count (default 64).
- [ ] `otlp_slab_alloc(slab)` returns a slot, marks it in-use.
- [ ] `otlp_slab_free(slab, slot)` returns the slot to the free list.
- [ ] Slab wraps `malloc` when full (fallback).
- [ ] Benchmark shows ≥ 3× speedup over malloc/free for the steady-state case.
- [ ] ASAN/TSan-clean.

## Why

malloc/free is the second-largest cost in the emit path (after the
encoder itself). For tracing tools that emit per-instruction or
per-syscall, that matters.

## Tradeoff

This adds a small but real heap accounting layer. Callers must
remember to free via the slab (not via `free()`). The slab's slot
type is `otlp_span_t` so the API is strongly-typed.

## Integration

The exporter's MPSC queue currently stores `otlp_span_t*`. With the
slab, the queue would store `otlp_span_handle_t` (slot token). Freeing
on the consumer side is through the slab. Emitter's
`otlp_exporter_emit_move()` continues to take ownership.
