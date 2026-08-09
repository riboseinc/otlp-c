# TODO 69 — Documentation accuracy audit (CLAUDE.md + architecture.md + README.md)

**Status:** Complete (v0.5.29)
**Priority:** P1

## What shipped (v0.5.29)

After v0.5.28 (async metric/log batching), the primary
documentation was stale by 13–16 releases:

| Doc | Last updated | Releases stale |
|---|---|---|
| CLAUDE.md | v0.5.16 | 13 |
| docs/architecture.md | v0.5.12 | 17 |
| README.md | v0.5.21 (partial) | 8 |

### CLAUDE.md fixes

1. **Version reference**: "All phases are complete (v0.5.15)" →
   "(v0.5.28)".
2. **Feature list**: added async metric/log batching (v0.5.28),
   W3C Baggage (v0.5.22), diagnostic callback (v0.5.23), typed
   Resource attributes (v0.5.24), metric temporality/is_monotonic
   (v0.5.26), HTTP timeout enforcement (v0.5.25), configurable
   flush timeout (v0.5.21), compile-time cap overrides (v0.5.27).
3. **Key files table**: updated exporter.c description (now "3
   MPSC queues"), context.h (now includes Baggage), http_client.c
   (now includes timeouts). Added bench/bench_emit.c entry.
4. **Conventions**: added three new bullets:
   - Three signals, one pipeline (describes the shared MPSC +
     tick architecture for spans/metrics/logs).
   - Diagnostics (set_logger callback, 7 events, zero overhead).
   - Stats (per-signal counters + global HTTP counters).

### docs/architecture.md fixes

1. **Pipeline description**: "Metrics and logs are flushed
   synchronously" → updated to describe v0.5.28's async pipeline
   for all three signals through shared MPSC queues.
2. **Module table**: exporter.c row updated from "MPSC queue" to
   "3 MPSC queues (span/metric/log)".

### README.md fixes

1. **Feature list**: Metrics "Synchronous flush" → "Async emit
   + synchronous flush fallback." Same for Logs.
2. **Context propagation**: added W3C Baggage alongside
   traceparent/tracestate.
3. **New bullets**: Diagnostics (set_logger), Resource attributes
   (typed).
4. **Status banner**: 0.5.18 → 0.5.28.

## Why this matters

CLAUDE.md is the first file every AI agent and contributor reads.
When it says "v0.5.15" while the codebase is at v0.5.28, agents
don't know about 13 releases of features. They miss the async
metric/log pipeline, the diagnostic callback, the timeout
enforcement — everything that makes the library production-ready.

This is the same failure mode fixed in TODO 57 (v0.5.16, CLAUDE.md
audit) and TODO 58 (v0.5.19, policy-docs audit). Each major
release batch accumulates stale claims; each audit cycle cleans
them. The cadence is now: audit, ship 10–15 releases, audit
again.

## Acceptance criteria
- [x] CLAUDE.md version reference matches version.h.
- [x] CLAUDE.md feature list includes all v0.5.16–v0.5.28 additions.
- [x] CLAUDE.md key files table reflects current module descriptions.
- [x] CLAUDE.md conventions include three-signal pipeline, diagnostics, stats.
- [x] architecture.md pipeline description reflects async metrics/logs.
- [x] README.md feature list reflects async metrics/logs + new features.
- [x] README.md status banner version matches version.h.
- [x] 33/33 tests pass (docs-only change, no behavioral risk).
