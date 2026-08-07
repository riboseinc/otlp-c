# TODO 52 — Documentation polish

**Status:** Complete (v0.5.11)
**Priority:** P2
**Depends on:** nothing

## Goal

Update README and docs to reflect the v0.5.x API surface.

## What shipped (v0.5.11)

**README.md**:
- Status updated from "0.1.0 (alpha)" to "0.5.10" with current
  feature list and platform support.
- New "Features" section listing all capabilities: traces, metrics,
  logs, context propagation, sampler, slab allocator, zero deps,
  no library threads, cross-platform.
- Description updated from "trace payloads (and, in future, metrics
  and logs)" to "all three signals — traces, metrics, and logs".

**docs/quickstart.md** (updated in v0.5.10):
- Code samples for metrics, logs, context propagation, sampling,
  and custom allocator.

## Acceptance criteria
- [x] README no longer references old version numbers.
- [x] README lists all current features.
- [x] Quickstart demonstrates all three signals.
- [x] All 27 tests pass.
