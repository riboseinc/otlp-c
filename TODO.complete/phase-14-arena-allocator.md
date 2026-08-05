# TODO 14 — Performance: arena allocator for encoder

**Status:** Pending
**Priority:** P2
**Branch:** future (v0.3+)

## Goal

The OTLP encoder allocates a `struct otlp_pb_buf` per sub-message
(Resource, Scope, Span, KeyValue, AnyValue). For a batch of 512
spans with 5 attributes each, that's ~5000 malloc calls per POST.
Replace with a single arena per top-level encode.

## Acceptance criteria

- [ ] `struct otlp_arena` allocator in `src/arena.{h,c}`. Single-grow buffer with bump pointer + rollback.
- [ ] `otlp_encode_export_trace_service_request` takes an arena parameter (or allocates one internally and exposes a free).
- [ ] Sub-message encode uses arena; no per-sub-message malloc.
- [ ] Benchmark shows ≥ 2× speedup vs current per-malloc approach.
- [ ] Property tests pass; ASAN-clean.

## Why

Per-malloc is the dominant cost in the encoder. For high-throughput
tracing (10k spans/sec) this is the bottleneck.

## Tradeoff

Arena adds an abstraction layer. Caller must free the arena after
the encoded body is consumed by the HTTP layer. Lifetime tracking
becomes more careful.
