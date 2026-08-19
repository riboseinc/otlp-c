# TODO 125 — Slab allocator: arena-aware realloc (UB fix)

**Status:** Complete (v0.5.85)
**Priority:** P1 (memory safety: libc realloc on a slab pointer is UB)

## What shipped

**Problem:** `otlp_install_slab_allocator` wrapped the global
allocator with `realloc` passed straight through to the previous
allocator. Any allocation served FROM THE ARENA that later grows
via `otlp_realloc` was therefore handed to libc `realloc` on a
pointer libc never allocated — undefined behavior. On macOS
libmalloc this aborts the process outright ("pointer being
realloc'd was not allocated"); on glibc it corrupts the heap.

Exposure (traced through the v0.5.75+ allocation profile): the
HTTP response buffer — `otlp_malloc(4096)` (arena-eligible when
`slot_size ≥ 4096`), then grown by doubling as the response
arrives. The documented `(128, 256)` sizing dodges that specific
path (4096 > 128 → malloc fallback), but attribute-vector growth
`realloc`-based sizing made the trap one tuning decision away for
every arena-eligible growing buffer. (Vec *items* themselves
always come from `realloc(NULL, …)` = malloc, which is why the
obvious span-attribute test passes — the regression test had to
target the allocator level to hit it.)

**Proof:** `prop_slab_global_realloc_growth` — install
slab(8192, 64), `otlp_malloc(4096)` (arena-served), fill,
`otlp_realloc(p, 8192)`. Pre-fix: **Abort trap 6** on the first
run. Post-fix: passes, contents preserved, clean uninstall.

**Fix:** `slab_realloc_hook` — arena pointers are MOVED (allocate
via the alloc hook at the new size, `memcpy(min(slot_size, n))`,
return the slot via the free hook); non-arena pointers pass
through; `realloc(NULL, n)` → alloc; `realloc(p, 0)` → free. The
`slot_size`-byte copy is safe: the original request was
`≤ slot_size`, so all live content is preserved (a growth
over-copies only dead bytes past the old request).

**Also:**
- `slab.c` header sizing note rewritten (the old "slot_size=128
  covers most attribute keys" pre-dates the vector model); any
  slot size is now SAFE; guidance is about hit rate only
  (128–256 B for strings/small vectors; spans ~176 B).
- quickstart/cookbook examples updated `(128, 256)` → `(256, 512)`.
- Two sign-conversion warnings in `test_http_parser.c` (v0.5.84
  leftovers) fixed.

## Verification

```
cmake --build build && cmake --build build-rel     # zero warnings
ctest --test-dir build -E http-timeout             # 38/38 (Debug)
ctest --test-dir build-rel -E http-timeout         # 38/38 (Release)
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -E "http-timeout|url-parse"  # 37/37
```
