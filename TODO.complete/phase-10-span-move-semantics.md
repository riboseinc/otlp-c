# TODO 10 — Performance: span move semantics

**Status:** Pending
**Priority:** P2
**Branch:** `v0.2-quality-pass` (later)

## Goal

Add an ownership-transferring emit path alongside the deep-copy path.
emit() currently clones the span (one malloc per attribute, one for
the name, one for the status message, one for the span struct). For
hot paths emitting thousands of spans, that's measurable.

## Acceptance criteria

- [ ] `otlp_exporter_emit_move(exp, span)` — takes ownership of `span`; caller must NOT free or touch it afterward.
- [ ] Existing `otlp_exporter_emit()` unchanged (deep copy, safe for callers that reuse the span).
- [ ] Document the tradeoff in `include/otlp-c/exporter.h`.
- [ ] Benchmark in `bench/bench_emit.c` (added under TODO 17) shows move is ~3× faster than clone for a 10-attribute span.

## Files

- `include/otlp-c/exporter.h` — add emit_move.
- `src/exporter.c` — implement.

## Why

Deep copy is the right default (callers can free immediately, matches
the documented API contract). But for tracing-hot paths (e.g.
libc-preload tracers that emit per-syscall) the malloc churn dominates.
A move alternative lets those callers opt in without breaking the
default-safe API.

## Tradeoff

Two emit paths is mild API bloat. Acceptable for v0.x.
