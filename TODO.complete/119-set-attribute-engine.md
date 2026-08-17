# TODO 119 — One set-attribute engine

**Status:** Complete (v0.5.79)
**Priority:** P1 (architecture: DRY/OCP/MECE — the setter machinery existed in 29 places)

## What shipped

**Problem:** after the v0.5.68–v0.5.78 arc, every public
attribute setter hand-rolled the same flow — null guards, value
pre-duplication, upsert reserve, typed fill, failure cleanup —
across **29 setters** in three files (7 span-level, 7 event, 7
link in span.c; 7 metric; 7 log). The duplication was load-bearing
in the bad way:

- the v0.5.75 NULL-guard bug existed precisely because a guard
  lived in a local helper that a later refactor deleted — with 29
  copies, proving "every setter guards correctly" meant reading
  29 bodies;
- every semantic refinement (upsert, build-then-attach,
  fill-cannot-fail) had to be replicated by hand;
- adding a surface or type multiplied the copies.

**Fix — the set-attribute engine:** three entry points in
`internal_util` own the entire flow:

- `otlp_attr_vec_set(vec, max, key, const otlp_value_t *)` —
  scalar values; duplicates owned payloads BEFORE the upsert
  reserve so the fill cannot fail;
- `otlp_attr_vec_set_array(…, items, n)` /
- `otlp_attr_vec_set_kvlist(…, entries, n)` — composites;
  build-then-attach (tree built first, freed if reserve fails).

Every public setter — all 29, on all six surfaces — is now a thin
typed wrapper: build a stack `otlp_value_t`, guard the surface
handle, call the engine. The event/link slot helpers are deleted
(the wrappers index the last event/link's attr vec directly, with
the same error-code ordering as before). Net: **−145 lines** while
adding the engine.

OCP: a future value type extends the engine once; the typed
wrappers stay two-liners. MECE: guard/OOM/upsert semantics exist
in exactly one place. Semantics: **byte-identical** — same error
codes for the same inputs, same ordering — verified by the
existing 36-test suite without modification.

## Verification

- Full suite 36/36 unchanged (including every setter unit test,
  upsert properties, wire properties, composite tests).
- 20,000-iteration property runs: attribute-roundtrip,
  resource-attrs, span — all pass.
- ASAN + LeakSanitizer clean.
- Benchmarks unchanged: ~155 ns/span (0 attrs), ~380 ns (5 attrs).
- Zero compiler warnings; clang-format applied.

## Sites changed

- `src/internal_util.{h,c}` — the three engine entry points.
- `src/span.c` — 21 setters → wrappers; slot helpers deleted.
- `src/metric.c`, `src/log.c` — 14 setters → wrappers.
- `CLAUDE.md` — conventions note the single engine.
- `docs/roadmap.md` — v0.5.77/v0.5.78 rows (promised catch-up).

```
ctest --test-dir build -E http-timeout          # 36/36
OTLP_C_PROPERTY_ITERS=20000 ./build/tests/property/otlp_property_attribute_roundtrip
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -E "http-timeout|url-parse"
```
