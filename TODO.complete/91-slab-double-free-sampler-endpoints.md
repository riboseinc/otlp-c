# TODO 91 — Slab double-free UB + sampler endpoint precision

**Status:** Complete (v0.5.51)
**Priority:** P1 (correctness: UB in slab) + P3 (precision: sampler)

## What shipped

Two defensive correctness fixes bundled:

1. **`otlp_slab_free_ptr` double-free UB.** The defensive
   "fall-through to free()" path was wrong: when the pointer was
   in the arena range but the slot wasn't marked used, the code
   called `free(ptr)` on an arena address. Arena is owned by the
   slab, not libc — calling `free()` on it is undefined behavior.
   ASAN flags it as "free() on non-heap pointer"; in production
   it silently corrupts the heap.

2. **`ratio_should_sample` endpoint precision.** The formula
   `scaled = trace_prefix / UINT64_MAX; if (scaled < ratio)` is
   off-by-one at the endpoints: ratio=1.0 with trace_prefix ==
   UINT64_MAX would not sample (1 missed in 2^64). The fix
   special-cases ratio <= 0.0 and ratio >= 1.0 for exact
   endpoints, and uses integer comparison `trace_prefix < threshold`
   in between — matching otel-cpp/java/go for cross-SDK
   consistency at the boundary.

## Sites changed

- `src/slab.c::otlp_slab_free_ptr` — when `ptr_in_arena && !used[i]`,
  return instead of calling `free(ptr)`. The "pointer is invalid"
  case is now a silent no-op.
- `src/sampler.c::ratio_should_sample` — endpoint short-circuits +
  integer comparison formula.
- `tests/property/test_property_slab.c` — added
  `prop_slab_double_free_no_crash`.
- `tests/property/test_property_sampler.c` — added
  `prop_ratio_one_samples_max_trace_id` and
  `prop_ratio_zero_drops_zero_trace_id`.

## Why bundled

Both fixes address correctness issues in self-contained modules
(slab, sampler). They were found in the same audit pass; each is
small enough that shipping them together keeps the release
overhead low.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 44/44 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

The double-free regression catches the slab UB under ASAN if it
regresses. The sampler endpoint tests catch the boundary
precision if the formula changes.

## Audit context

Continues the audit pattern:
- v0.5.48: OTLP schema field numbers.
- v0.5.49: ExponentialHistogram wire types.
- v0.5.50: LogRecord trace_id/span_id independence.
- v0.5.51: slab double-free UB + sampler endpoints.

The slab fix is unusual in that it's a memory-safety issue
(undefined behavior) rather than a wire-format issue. The audit
pattern (cross-check against upstream + general C correctness)
keeps surfacing real bugs across module types.
