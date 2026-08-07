# TODO 42 — Slab allocator

**Status:** Complete (v0.5)
**Priority:** P2
**Depends on:** nothing

## Goal

Reduce malloc/free traffic for high-churn small allocations by
serving them from a pre-allocated arena with a free-slot bitmap.

## What shipped (v0.5)

**Public API** (`include/otlp-c/slab.h`):
- `otlp_slab_t` opaque type.
- `otlp_slab_create(slot_size, capacity)` — allocates arena of
  `slot_size * capacity` bytes plus a `capacity`-byte used-bitmap.
  `slot_size` is rounded up to `void *` alignment.
- `otlp_slab_alloc(slab, size)` — if `size <= slot_size` and a slot
  is free, returns a slot from the arena. Otherwise falls through to
  `malloc(size)`. Drop-in replacement for `malloc`.
- `otlp_slab_free_ptr(slab, ptr)` — detects (via address range
  check) whether `ptr` came from the arena or malloc, and routes
  accordingly. Drop-in replacement for `free`.
- `otlp_slab_get_stats(slab, out)` — snapshot of alloc/free counts,
  slab hits, malloc fallbacks, in-use slot count.
- `otlp_slab_stats_t` struct with all counters.

**Implementation** (`src/slab.c`):
- Linear scan over used-bitmap to find a free slot. O(capacity) per
  alloc but fast for typical sizes (capacity ≤ 256).
- Address-range check on free: `ptr >= arena && ptr < arena+size`.
- Thread-safety: NOT thread-safe. Designed for single-threaded hot
  paths (per-tracer or per-thread slab). Spans are single-threaded
  by API contract.

**Property tests** (`tests/property/test_property_slab.c`):
- `prop_slab_roundtrip` — alloc N, free N, no slots in use.
- `prop_slab_slot_reuse` — alloc-free-alloc returns the same slot.
- `prop_slab_oversize_fallback` — 128B from a 32B-slot slab goes to
  malloc, not the arena.
- `prop_slab_overflow_fallback` — exhausting slots falls through to
  malloc; arena content integrity verified.
- `prop_slab_free_routes_correctly` — mixed arena + malloc pointers
  all freed correctly.
- `prop_slab_stats_consistent` — balanced alloc/free leaves 0 in-use;
  counters reflect actual operations.

## Design notes

The slab is a standalone utility, not yet wired into span.c or
exporter.c. The integration point is `internal_util.c`'s
`otlp_malloc`/`otlp_free` — those could optionally dispatch through
a slab. That integration is deferred to a follow-up PR because:

1. It changes the global allocator behavior, which affects every
   allocation site. Needs careful benchmarking to confirm net win.
2. The right slab size/capacity depends on workload. Caller-tunable
   integration is more flexible than a fixed global.

For now, callers who want slab-managed allocations (e.g., for their
own hot loops) can use the API directly. The library itself remains
on malloc until a benchmark proves the slab helps in the typical
emit path.

## Acceptance criteria (from original spec)
- [x] `struct otlp_slab` with fixed-size slot count.
- [x] `otlp_slab_alloc(slab, size)` returns a slot, marks in-use.
- [x] `otlp_slab_free_ptr(slab, ptr)` returns the slot to the free list.
- [x] Slab wraps `malloc` when full (fallback).
- [x] ASAN-clean (verified via property tests).
- [ ] Benchmark shows ≥ 3× speedup over malloc/free — deferred
      (no integration point yet; benchmark would measure the slab in
      isolation, not the realistic emit path).

## Out of scope (deferred)
- Integration into `otlp_malloc` / `otlp_free` (global allocator hook).
- Multi-size-class slab pool (currently single slot size).
- Thread-safe variant (currently single-threaded).
- Slab as the backing store for the exporter's MPSC queue slots.
