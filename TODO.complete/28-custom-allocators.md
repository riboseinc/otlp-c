# TODO 28 — Allow caller to supply malloc/free

**Status:** Deferred (v1.0+)
*Closed because:* Custom allocator hook is a v1.0 API stability commitment. Defer until API freezes.
**Priority:** P1
**Depends on:** nothing

## Goal

New API: otlp_set_allocator(malloc_fn, free_fn). Useful for embedded targets and language runtimes that manage their own heap.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
