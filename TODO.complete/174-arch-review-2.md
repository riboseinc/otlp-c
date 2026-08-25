# TODO 174 — Second architecture review, fully implemented

**Status:** Complete (v1.0.5)
**Priority:** P1 (C1 code), P2 (C2/C3 docs)

Report: $TMPDIR/architecture-review-20260825-094455.html — the
first review's three candidates all shipped (v0.6.11-13); this
pass found what the refactors left behind.

## C1 (Strong, implemented) — one retry engine

flush_sync's hand-rolled backoff (fixed initial, clamped 100ms)
vs the async path's jittered policy: two engines, no locality.
Now: otlp_retry_delay_ms with cfg {initial, min(max,100)} — the
sync cap lives in config, sync gains jitter, property tests
cover the timing.

## C2 (Strong, implemented) — the website

No website existed. GitHub Pages now serves the generated
Doxygen reference (riboseinc.github.io/otlp-c), deployed by CI
on every push to main (guarded: main-push only; Pages configured
via API to workflow source). Generated => cannot go stale.
CONTEXT.md added to the Doxygen INPUT.

## C3 (Worth exploring, implemented) — CONTEXT.md

Domain glossary: 21 terms, one line each, home-module pointers.

## C4 (Speculative, declined) — internal_util split

Two review cycles, zero friction incidents. Recorded in the
report and CHANGELOG so the next review doesn't re-litigate
without new evidence.

## Docs sweep

README: stale "v0.6.8 shown" text + FetchContent tags → current;
website link at the top. quickstart: website link + tag.
CLAUDE.md: CONTEXT.md + website in See-also.
