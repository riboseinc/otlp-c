# TODO 116 — Span events/links grow-on-demand; span struct 176 bytes

**Status:** Complete (v0.5.76)
**Priority:** P1 (performance: the last inline bulk in the span)

## What shipped

**Problem (completing the arc):** after v0.5.75 the span struct was
5,776 bytes, of which the inline `events[64]` (2,560 B) and
`links[64]` (3,072 B) header arrays were ~97%. A span with zero
events and zero links — the overwhelmingly common case — allocated
and zeroed all of it on every `otlp_span_create` and every clone
inside `otlp_exporter_emit`.

**Fix:** events and links follow the same grow-on-demand model as
the attribute vectors — heap arrays that start NULL, grow
4 → 8 → … slots via `realloc` (bounded by the 64-event / 64-link
caps; new tail zeroed so each slot's name/attr-vec start valid),
with two small typed grow helpers in span.c. Caps and overflow
semantics unchanged. `struct otlp_event` / `struct otlp_link`
layouts are unchanged, so the encoder's accessor-based reads are
untouched.

**Result (measured, 5,000-iteration bench rows — the stable ones
on this machine):**
- `sizeof(struct otlp_span)`: 5,776 → **176 bytes** (32.9×). For
  perspective: 138,880 B at v0.5.67 → 8,832 (v0.5.68) → 5,776
  (v0.5.75) → **176** (v0.5.76) — a 789× reduction across the arc.
- Zero-attribute span emit: ~885 → **~150 ns/span**
  (~6.6M spans/s through the full clone + queue + tick pipeline).
- 5-attribute span emit: ~530 → ~375 ns/span.

## Sites changed

- `src/span.c` — struct (pointer + n + cap × 2), `event_grow` /
  `link_grow` helpers, `otlp_span_add_event` / `add_link` grow,
  `otlp_span_free` frees the arrays. Clone paths unchanged (they
  call add_event/add_link, which now grow).
- `tests/unit/test_unit_span.c` — struct-size budget 8 KB → 512 B;
  new `test_event_link_overflow` (64 OK, 65th →
  `OTLP_ERR_OVERFLOW`, both targets) — the caps had no direct test
  before.

## Safety

- Realloc failure leaves the old array intact; the failed add frees
  its duplicated name (event path) and returns `OTLP_ERR_NOMEM`.
- All direct events/links access was already confined to span.c
  (everything else goes through `otlp_span_get_events`/`get_links`,
  whose signatures are unchanged).
- Full suite 36/36; ASAN + LeakSanitizer clean; the fail-injecting
  OOM sweep passes over the new growth paths.

## Verification

```
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout          # 36/36
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -E "http-timeout|url-parse"
./build/bench/otlp_bench_emit   # ~150 ns/span (0 attrs, 5k rows)
```
