# TODO 57 — CLAUDE.md accuracy audit

**Status:** Complete (v0.5.16)
**Priority:** P1

## What shipped (v0.5.16)

CLAUDE.md — the file that guides every future contributor and AI
agent — had 5 stale claims from the v0.1.0 bootstrap era:

1. "emits trace spans (and, in future, metrics and logs)" → updated
   to "all three signals — traces, metrics, and logs"
2. "Stubbed-default builds... link a working stub library" → updated
   to "Clean default builds... no stubs, no OTLP_ERR_NOT_IMPLEMENTED"
3. "For the implementing agent: Phase 1-8 work through roadmap" →
   replaced with "All phases complete" + extension guide (OCP
   patterns for adding new attribute types, metric types, samplers)
4. "The stubs in src/*.c are placeholders... return NOT_IMPLEMENTED" →
   removed (no stubs exist)
5. "the exporter holds a mutex around batch emission" → corrected to
   "the library is lock-free... atomics + MPSC queue"

Also updated the "Key files to know" table: added metric.h, log.h,
sampler.h, context.h, slab.h, otlp_schema.h, otlp_metrics_encoder.c,
otlp_logs_encoder.c, atomic_compat.h. Removed stale references to
build.yml (now ci.yml) and exporter_otel.c (now part of exporter.c).

## Why this matters

CLAUDE.md is the first file every AI agent reads when working on the
project. Stale claims cause agents to re-do completed work, look for
stubs that don't exist, and add mutexes where atomics are used.
This fix prevents wasted effort and incorrect changes.

## Acceptance criteria
- [x] No stale claims about stubs, phases, mutexes, or "future" features.
- [x] Key files table includes all current modules.
- [x] Extension guide documents OCP patterns for common additions.
- [x] All 27 tests pass.
