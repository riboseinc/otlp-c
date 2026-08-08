# TODO 54 — Slab free-list + benchmark + ExpHistogram setter

**Status:** Complete (v0.5.13)
**Priority:** P2

## What shipped (v0.5.13)

### Slab allocator performance fix

The slab's `otlp_slab_alloc` used a linear scan over the `used[]`
bitmap to find a free slot — O(capacity) per allocation. Benchmark
showed it was 13× slower than system malloc (429 ns/op vs 32 ns/op).

Replaced the linear scan with a free-list stack (LIFO array of slot
indices). Alloc pops from the stack (O(1)); free pushes (O(1)).
Benchmark now shows 36 ns/op — near-parity with system malloc.

Also fixed an infinite-recursion bug in the alloc/free fallback
paths: `otlp_malloc` → `slab_alloc_hook` → `otlp_slab_alloc` →
`otlp_malloc` → ... Changed fallbacks to use libc `malloc`/`free`
directly.

### Benchmark

`bench/bench_slab.c`: 100K alloc+free cycles of 64-byte objects.
Measures ns/op for system malloc vs slab allocator. Prints speedup
ratio.

### ExponentialHistogram setter

`otlp_metric_set_exp_histogram(m, scale, pos_offset, pos_counts,
pos_n, neg_offset, neg_counts, neg_n)`: sets all ExponentialHistogram
bucket data in one call. The library copies the arrays. Caller
manages bucket-index computation (application-specific scale).

## Acceptance criteria
- [x] Slab allocator uses O(1) free-list (not linear scan).
- [x] Benchmark runs without segfault.
- [x] Slab speedup ≥ 0.5× (near-parity with optimized malloc).
- [x] ExpHistogram setter API added.
- [x] All 27 tests pass.
