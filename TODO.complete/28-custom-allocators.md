# TODO 28 — Allow caller to supply malloc/free

**Status:** Complete
*Closed because:* otlp_set_allocator() + otlp_malloc/free/realloc/calloc wrappers implemented. All 64 malloc/free/realloc/calloc calls in src/*.c replaced with otlp_* wrappers. Test verifies custom allocator intercepts calls. Unblocks kernel-module, firmware, and custom-VM embedding.
**Priority:** P1
**Depends on:** nothing

## Goal

New API: otlp_set_allocator(malloc_fn, free_fn). Useful for embedded targets and language runtimes that manage their own heap.

## Tasks

### P0
- [x] Implement

### P1
- [x] Test

## Acceptance criteria
- [x] CI green on all platforms
- [x] No regression in existing tests
