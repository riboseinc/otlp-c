# TODO 79 — Check `otlp_pb_buf_init` returns + batch encode benchmark

**Status:** Complete (v0.5.39)
**Priority:** P3 (defensive coding + tooling)

## What shipped

Two related improvements bundled in one release:

1. **Defensive coding fix.** `try_start_metric_post` and
   `try_start_log_post` called `otlp_pb_buf_init(&body, ...)` without
   checking the return value. On allocation failure, the encoder
   would write into a zero-cap buffer and the failure attribution
   would land on the encoder, not the allocation. Both sites now
   capture and return the init status directly.

2. **Batch encode microbenchmark.** Added `bench/bench_encode_batch.c`
   that measures batch-encode throughput at batch sizes 1, 16, 64, 256,
   512 with 0 or 5 attributes per span. Catches O(n^2) regressions in
   the encoder and verifies the v0.5.38 pre-sized buffer optimization.

## Sites changed

- `src/exporter.c::try_start_metric_post` — check init return.
- `src/exporter.c::try_start_log_post` — check init return.
- `bench/bench_encode_batch.c` — new file.
- `bench/CMakeLists.txt` — register new bench target.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_BUILD_BENCH=ON
cmake --build build
./build/bench/otlp_bench_encode_batch   # linear O(n) scaling
ctest --test-dir build --output-on-failure  # 34/34 pass
```

Output is reported as total ns, ns/span, and wire bytes per
configuration. The benchmark is opt-in (requires `-DOTLP_C_BUILD_BENCH=ON`)
and excluded from the default build, consistent with the existing
`otlp_bench_encode`, `otlp_bench_slab`, and `otlp_bench_emit` targets.
