# TODO 109 — Metric/log structs 19×/52× smaller; log emit 5× faster

**Status:** Complete (v0.5.69)
**Priority:** P1 (performance: per-record cost of the two remaining signals)

## What shipped

**Problem found by measurement (the v0.5.68 follow-up):** the span
fix left the other two signals untouched. Measuring them:

- `sizeof(struct otlp_metric)` = **4,312 bytes** — `attrs[128]`
  inline was 4,096 bytes (**94.8%** of the struct).
- `sizeof(struct otlp_log_record)` = **4,168 bytes** — `attrs[128]`
  inline was 4,096 bytes (**98.3%**).

Every `otlp_metric_create`/`otlp_log_record_create` calloc'd and
zeroed a 4KB attribute array, and every `emit_metric`/`emit_log`
clone copied it — even though most metrics carry no attributes and
many log records are bare severity+body. Logs are the
highest-volume signal, so this was the dominant per-record cost
after v0.5.68.

**Fix:** same lazy-allocation pattern the span's events/links got
in v0.5.68 — `attrs` is now a `struct otlp_attribute *`, NULL
until the first attribute is set, `calloc(128, ...)` on first use.
Caps unchanged (128 per metric / per record).

While touching both release paths, the hand-rolled
free-key-then-switch-on-type loops were replaced with the shared
recursive `otlp_attribute_free` from `internal_util.c` — the
metric and log versions were triplicating the span's logic and
would have silently leaked nested array/kvlist values had those
types ever been allowed on metrics/logs.

**Result:**
- `sizeof(struct otlp_metric)`: 4,312 → **224 bytes** (19.3×).
- `sizeof(struct otlp_log_record)`: 4,168 → **80 bytes** (52×).
- `otlp_bench_logs` (new, clone + queue + tick, null_transport),
  0-attribute records: ~590-720 → **~110-175 ns/log** (≈5×,
  >9M logs/s), and consistent across batch sizes. Records that do
  carry attributes are unchanged within noise — they still
  allocate the 128-slot array on first attribute set; the win is
  the attribute-less record, the common logging case.

## How it was found

v0.5.68 measured only the span. The natural follow-up question —
"what do the other two signals' structs look like?" — needed a
one-off harness (`#include "metric.c"` + `printf sizeof`). Both
structs had the identical inline-array smell at smaller scale.

## Sites changed

- `src/metric_internal.h` / `src/log_internal.h` —
  `attrs[128]` → `*attrs` (NULL until first set), with rationale
  comments; added test-only `otlp_metric_struct_size()` /
  `otlp_log_struct_size()` accessors (structs stay opaque).
- `src/metric.c` — `metric_reserve_attr` lazy-callocs;
  `metric_release_attrs` uses shared `otlp_attribute_free` and
  frees the array; `otlp_metric_clone` callocs the destination
  array before `otlp_attribute_copy_all` (zeroed destination so
  free is safe on copy failure); struct-size accessor.
- `src/log.c` — same four changes via `log_attrs_reserve`.
- `tests/unit/test_unit_metric.c`, `tests/unit/test_unit_log.c` —
  new; known-answer tests for setters/clone plus the lazy
  contract (`get_attrs` returns NULL/0 before any attribute) and
  struct-size budget assertions (≤1KB / ≤512B).
- `bench/bench_logs.c` — new; dual-pass (emit vs emit_move) log
  throughput, mirroring `bench_emit.c`.
- `bench/bench_emit.c` — drop an unused variable left by the
  v0.5.68 dual-pass refactor (build warning).
- `bench/CMakeLists.txt`, `tests/unit/CMakeLists.txt` — register
  the new targets.

## Safety

- Encoders read attributes only through `otlp_metric_get_attrs` /
  `otlp_log_get_attrs`; with `n_attrs == 0` the returned NULL is
  never dereferenced (loops don't execute) — same argument as
  v0.5.68.
- The fail-injecting OOM tests probe every allocation offset in
  clone; the new callocs are covered.
- Full suite (36/36) + ASAN clean.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_BUILD_BENCH=ON
cmake --build build
ctest --test-dir build -E http-timeout          # 36/36
cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan && ctest --test-dir build-asan -E http-timeout
./build/bench/otlp_bench_logs                   # ~110-175 ns/log (0 attrs)
```
