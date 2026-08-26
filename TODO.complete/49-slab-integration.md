# TODO 49 — Slab integration into global allocator

**Status:** Complete (v0.5.4)
**Priority:** P3
**Depends on:** nothing (TODO 42 ships standalone slab)

## Goal

Wire the `otlp_slab_t` allocator (shipped in v0.5.0 as standalone)
into `otlp_malloc` / `otlp_free` so common small allocations in
the hot emit path go through the slab without callers changing.

## Background

The standalone slab (TODO 42) reduces malloc/free traffic for
high-churn small allocations, but the library still uses `malloc`
for everything via `internal_util.c`. To benefit from the slab,
callers must manually opt in by managing their own slab — not
transparent.

The integration point is the custom allocator hook
(`otlp_set_allocator()`). The slab could wrap the existing
allocator, intercepting small allocations and serving them from
a pre-allocated arena.

## Design

**New public API** (`include/otlp-c/slab.h` extension):
```c
/* Install a slab-backed allocator as the process-wide allocator.
 * Subsequent otlp_malloc / otlp_free calls route through the slab
 * for allocations <= slot_size; everything else falls through to
 * the previously-installed allocator.
 *
 * Returns OTLP_ERR_NOMEM if the arena can't be allocated. */
OTLP_C_EXPORT
otlp_status_t otlp_install_slab_allocator(size_t slot_size,
                                          size_t capacity);

/* Uninstall the slab allocator; restores the previous allocator.
 * Frees the arena. Existing pointers returned by otlp_malloc are
 * NOT freed automatically — caller must free them first. */
OTLP_C_EXPORT
void otlp_uninstall_slab_allocator(void);
```

**Implementation** (`src/slab.c`):
```c
static otlp_slab_t *g_slab = NULL;
static otlp_allocator_t g_prev_allocator;

static void *slab_alloc(size_t n) {
    if (g_slab && n <= g_slab->slot_size)
        return otlp_slab_alloc(g_slab, n);
    return g_prev_allocator.alloc(n);
}

static void slab_free(void *p) {
    /* otlp_slab_free_ptr routes arena-vs-malloc automatically */
    if (g_slab)
        otlp_slab_free_ptr(g_slab, p);
    else
        g_prev_allocator.free(p);
}

otlp_status_t
otlp_install_slab_allocator(size_t slot_size, size_t capacity)
{
    otlp_slab_t *s = otlp_slab_create(slot_size, capacity);
    if (!s)
        return OTLP_ERR_NOMEM;
    g_prev_allocator = *otlp_get_allocator();
    g_slab = s;
    otlp_allocator_t wrapped = { slab_alloc, g_prev_allocator.realloc, slab_free };
    otlp_set_allocator(&wrapped);
    return OTLP_OK;
}
```

**Benchmark** (`bench/slab_bench.c`):
- Emit 1M spans with and without slab allocator installed
- Measure malloc/free count and total time
- Target: ≥ 3× speedup on the emit path (per TODO 42 acceptance)

## Acceptance criteria
- [x] `otlp_install_slab_allocator` / `_uninstall` implemented.
- [x] Existing code (span, exporter, etc.) transparently uses slab.
- [x] Benchmark shows ≥ 3× speedup on emit-heavy workload.
- [x] ASAN-clean.
- [x] Property tests pass (slab routes correctly via address check).
- [x] Backwards compat: default allocator unchanged if slab not installed.

## Out of scope (deferred further)
- Multi-size-class slabs (currently single slot_size).
- Thread-local slabs (currently process-wide single slab).
