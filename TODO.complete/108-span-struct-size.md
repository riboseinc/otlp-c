# TODO 108 — Span struct 15.7× smaller; emit 20× faster

**Status:** Complete (v0.5.68)
**Priority:** P1 (performance: the dominant emit-path cost)

## What shipped

**Problem found by measurement:** `sizeof(struct otlp_span)` was
**138,880 bytes**. The embedded `events[64]` and `links[64]`
arrays each carried an inline `attrs[32]` array — 1KB per
event/link slot — so a span with zero events and zero links still
allocated and zeroed 139KB on every `otlp_span_create` and
`otlp_span_clone`. The clone inside `otlp_exporter_emit` made
this the dominant emit cost, and the huge alloc/free cycles made
benchmark timings erratic.

**Fix:** event/link attribute arrays are now lazily heap-allocated
pointers (`calloc(32, ...)` on first attribute set). A typical
event/link with zero attributes costs one NULL pointer. Caps
unchanged (32 attrs per event/link).

**Result:**
- `sizeof(struct otlp_span)`: 138,880 → **8,832 bytes** (15.7×).
- `otlp_bench_emit`: ~30,000 ns/span → **~1,500 ns/span**
  (~30K spans/s → **~650K spans/s**), and consistent.

## How it was found

Ran the benchmarks (they existed since v0.5.17/v0.5.39 but the
emit numbers had never been attributed). Split bench_emit into
clone-API (`emit`) vs build+move (`emit_move`) passes — both were
slow, pointing at shared per-span cost rather than the clone.
Measured the struct: 139KB, 96% of it the inline event/link
attribute arrays.

## Sites changed

- `src/span_internal.h` — `struct otlp_event` / `struct otlp_link`:
  `attrs[N]` → `*attrs` (NULL until first set); added
  `otlp_span_struct_size()` test accessor.
- `src/span.c`:
  - `set_event_attribute_string` / `set_link_attribute_string` —
    lazy-allocate the attrs array (calloc, cap-checked).
  - `otlp_span_free` — free each event/link attrs array after the
    per-attribute frees.
  - `otlp_span_clone` — calloc the destination attrs array before
    `otlp_attribute_copy_all` (zeroed destination so span_free is
    safe on copy failure).
  - `otlp_span_struct_size` — new test-only accessor.
- `tests/unit/test_unit_span.c` — `test_span_struct_size` asserts
  ≤ 16KB; prints the actual size.
- `bench/bench_emit.c` — dual-pass (emit vs emit_move) output.

## Safety

- The encoder reads `events[i].attrs` with `n_attrs == 0` → loop
  doesn't execute; NULL-safe.
- The fail-injecting OOM tests (`test_span_clone_oom`) probe every
  alloc offset in `span_clone`, covering the new calloc calls —
  all pass, so the lazy paths are leak-free under OOM.
- Full suite + ASAN clean.

## Verification

```
cmake --build build
ctest --test-dir build -E http-timeout    # 34/34 + size guard
cmake --build build-asan && ctest --test-dir build-asan -E http-timeout
./build/bench/otlp_bench_emit             # ~1,500 ns/span
```
