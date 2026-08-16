# TODO 110 — One owner for the lazy attribute-list model (DRY across 4 sites)

**Status:** Complete (v0.5.70)
**Priority:** P2 (cleanliness: removes quadruplicated storage-model code)

## What shipped

**Problem:** after v0.5.68/v0.5.69, the same "lazy attribute array"
storage model was implemented four times — span events, span links,
metrics, log records — and each site hand-rolled three copies of its
logic:

- *reserve*: cap check → lazy `calloc(cap)` on first use → dup key →
  fill slot (4 copies)
- *copy* (clone paths): `calloc` a zeroed cap-sized array →
  `otlp_attribute_copy_all` → set count, with OOM-safe cleanup (4
  copies)
- *free*: per-attribute `otlp_attribute_free` loop → free the array →
  NULL the pointer (4 copies)

Twelve near-identical blocks. Any fix to one (an OOM path, a cap
semantics change) had to be replicated by hand — the v0.5.68/69
histories show exactly that: each site was patched separately.

**Fix:** the model now has one owner. `internal_util.{h,c}` gained
three functions:

- `otlp_attr_list_reserve(attrs, n, cap, key, out)` — lazily
  allocate + reserve a slot; caller fills value and increments on
  success.
- `otlp_attr_list_copy(dst, n_dst, cap, src, n_src)` — deep-copy
  into a fresh lazily-styled array; frees everything on failure.
- `otlp_attr_list_free(attrs, n)` — release all + the array; resets
  to NULL/0.

The attribute-bearing types just pass their `(attrs, n_attrs, cap)`
triple. OCP: a future attribute-bearing type gets all three
behaviors from one call site each. MECE: the storage model lives in
exactly one file.

Deleted: `metric_reserve_attr`, `metric_release_attrs`,
`log_attrs_reserve`, `log_release_attrs`, and the inline copies in
`otlp_span_set_event_attribute_string`,
`otlp_span_set_link_attribute_string`, `otlp_span_free`, and both
clone paths. Net: 12 blocks → 3 functions + 12 one-line calls.

Also fixed (found while building): the `CPACK_SOURCE_IGNORE_FILES`
entry in `CMakeLists.txt` had jammed-together quoted fragments that
produced a CMake author warning on every configure; rewritten as a
plain list (same semantics — CMake splits unquoted-adjacent list
items on `;` boundaries either way).

## Behavior changes

None intended. The reserve helper preserves the exact contract the
sites already used: it does NOT increment the count — callers
increment only after the value allocation succeeds, so a failed
set leaves the object unchanged. Verified by:

- 36/36 tests (unit, property, OOM injection, stress)
- ASAN clean (the fail-injecting allocator-oom test probes every
  allocation offset in the clones, which now go through
  `otlp_attr_list_copy`)
- Benchmarks unchanged: ~110 ns/log, ~1,500 ns/span (0 attrs)
- Zero compiler warnings (one doc-comment `/*` trip fixed in the
  new header text)

## Sites changed

- `src/internal_util.h` / `src/internal_util.c` — the three
  `otlp_attr_list_*` functions + the model's contract docs.
- `src/span.c` — event/link attribute setters, `otlp_span_free`
  loops, `otlp_span_clone` event/link paths.
- `src/metric.c` — three `set_attribute_*` setters,
  `otlp_metric_free`, `otlp_metric_clone`; local helpers deleted.
- `src/log.c` — both `set_attribute_*` setters,
  `otlp_log_record_free`, `otlp_log_record_clone`; local helpers
  deleted.
- `CMakeLists.txt` — CPACK ignore-list quoting.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_BUILD_BENCH=ON
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout   # 36/36
cmake --build build-asan && ctest --test-dir build-asan -E http-timeout
./build/bench/otlp_bench_logs  && ./build/bench/otlp_bench_emit
```
