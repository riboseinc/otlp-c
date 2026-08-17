# TODO 115 — Grow-on-demand attribute vectors everywhere

**Status:** Complete (v0.5.75)
**Priority:** P1 (performance: attribute-bearing objects paid cap-sized allocations)

## What shipped

**Problem (the v0.5.68/69 follow-through):** lazy allocation made
attribute-less objects cheap, but the FIRST attribute on an object
allocated the full cap-sized array:

- metric / log record: 128 slots = **4 KB** calloc'd + zeroed for a
  1-attribute object;
- span event / link: 32 slots = 1 KB;
- the span itself still carried the inline `attrs[128]` — 4 KB
  allocated and zeroed on every create and clone, whether used or
  not.

**Fix:** one storage model — `struct otlp_attr_vec { items, n, cap }`
(span_internal.h) — embedded by every attribute-bearing object
(span, event, link, metric, log record). The shared reserve grows
the array on demand: 4 slots initially, doubling (via `realloc`,
bounded by the owner's max), zeroing only the new tail. Clone
copies exact-fit (`otlp_attr_vec_copy` — n slots, no spare).

The span's own attributes joined the shared model (the separate
inline-array `attr_reserve` path in span.c is deleted), completing
the MECE story: one vector type, three vec helpers in
internal_util, five owners.

**Result (measured):**
- `sizeof(struct otlp_span)`: 8,832 → **5,776 bytes** (−35%; the
  128 event/link header slots now dominate).
- `sizeof(struct otlp_metric)`: 232 B, log record: 88 B (+8 B for
  the capacity field).
- `otlp_bench_emit` (clone + queue + tick): attrs=1 span emit
  ~1,375 → **~350 ns** (≈4×); attrs=5 ~1,590 → ~530 ns; the
  zero-attribute case is unchanged-to-better depending on run.
- Attribute-bearing log/metric paths no longer pay a 4 KB calloc
  for the first attribute.

**Bug found and fixed during conversion:** the span-level setters
had lost their `!span` NULL guards when the guard-carrying local
reserve was removed in an earlier refactor — `set_attribute(NULL,
…)` segfaulted instead of returning `OTLP_ERR_NULL`. Caught
immediately by `prop_setters_null_safe`; guards restored on all
span-level setters (verified by script over every setter body).

## Sites changed

- `src/span_internal.h` — `struct otlp_attr_vec`; event/link embed it.
- `src/metric_internal.h`, `src/log_internal.h` — embed the vec.
- `src/internal_util.{h,c}` — `otlp_attr_vec_reserve` (grow:
  4→8→…→max via realloc, zeroing the new tail; realloc failure
  leaves the old array intact), `otlp_attr_vec_copy` (exact-fit),
  `otlp_attr_vec_free`; the old list helpers deleted.
- `src/span.c` — struct swap; local `attr_reserve` +
  `span_release_attrs` deleted; setters/free/clone/accessors on
  the vec helpers; NULL guards restored.
- `src/metric.c`, `src/log.c` — mechanical swap to vec helpers.
- `src/otlp_messages.c` — event/link attribute reads
  (`…attrs.items`, `…attrs.n`).
- `tests/` — layout references updated; struct-size budgets
  tightened (span ≤ 8 KB, metric ≤ 512 B, log ≤ 256 B).

## Safety

- Growth uses `otlp_realloc` through the custom allocator; on
  failure the old array is untouched and `n` is unchanged.
- The fill-cannot-fail reserve contract is preserved (values are
  pre-built; append commits after growth succeeds).
- Upsert unchanged: overwrite releases the old value, works at max.
- Full suite 36/36; ASAN + LeakSanitizer clean (Linux CI is the
  authoritative leak gate; local macOS noise is framework-only);
  fail-injecting OOM sweep passes over the new growth/copy paths.

## Verification

```
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout          # 36/36
OTLP_C_PROPERTY_ITERS=20000 ctest --test-dir build -L property
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -E http-timeout
./build/bench/otlp_bench_emit && ./build/bench/otlp_bench_logs
```
