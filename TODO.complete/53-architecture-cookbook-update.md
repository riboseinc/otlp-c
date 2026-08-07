# TODO 53 — Architecture + cookbook documentation update

**Status:** Complete (v0.5.12)
**Priority:** P2

## What shipped (v0.5.12)

**docs/architecture.md**: comprehensive rewrite reflecting the v0.5.x
multi-signal architecture. Updated the layered view (all 21 modules),
module MECE table (20 rows), and design patterns section
(model-driven encoding, table-driven dispatch, caller-driven I/O,
lock-free MPSC). Fixed stale claims (mutex → lock-free, arena → slab,
traces-only → all three signals).

**docs/cookbook.md**: added sections 6-10 with copy-pasteable
patterns for metrics, logs, context propagation, sampling, and
custom allocator/slab.

## Acceptance criteria
- [x] architecture.md reflects all current modules.
- [x] architecture.md documents design patterns (OCP, DRY, lock-free).
- [x] cookbook.md has patterns for all 3 signals + sampling + allocator.
- [x] All 27 tests pass.
