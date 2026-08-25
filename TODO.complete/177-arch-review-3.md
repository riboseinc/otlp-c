# TODO 177 — Third architecture review, fully implemented

**Status:** Complete (v1.1.2)
**Priority:** P1 (C1 module shape), P2 (C2/C3 docs)

## C1 (Strong, implemented) — the sync-flush pipeline leaves exporter.c

exporter.c held BOTH delivery engines (async tick pipeline +
synchronous flush) plus the exporter struct: 1865 lines, and
understanding flush_metric meant scrolling past the whole tick
machine. Now:

- struct otlp_exporter + signal_state + signal-kind enum moved to
  exporter_internal.h — the ONE internal seam, now explicit.
- src/exporter_sync.c (330 lines): flush_post_once, flush_sync,
  flush_metric, flush_log — one-shot encode → POST → retry →
  events, all sync semantics in one readable file.
- exporter.c: 1865 → 1471 lines (lifecycle + async pipeline).
- event_log and report_partial_success became shared internal
  symbols (otlp_exporter_event_log /
  otlp_exporter_report_partial_success) — one diagnostics model,
  two entry points.
- Public surface unchanged; all 52 tests pass unmodified.

## C2 (Strong, implemented) — the site's feature blind spots

- /docs/propagation/ — W3C Trace Context + Baggage: inject/extract
  with carrier callbacks, the spec-exact guarantees, sampler
  interaction. context.h is now documented on the site.
- /docs/performance/ — the real numbers, how to reproduce (bench
  preset), why it's fast, the measurement discipline.
- Docs sidebar + sitemap updated (13 pages).

## C3 (Worth exploring, implemented) — architecture.md diagram

env_config.c added to the layer diagram (in the module table since
0.7.1, missing from the diagram).

## Declined (third review running)

internal_util split — unchanged verdict.
