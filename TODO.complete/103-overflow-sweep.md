# TODO 103 — Integer overflow sweep (allocation sites)

**Status:** Complete (v0.5.63)
**Priority:** P0 (memory safety: completes the overflow audit)

## What shipped

v0.5.62 fixed the metric allocation paths. This release sweeps
every remaining `count * sizeof(...)` site in `src/`:

1. **`otlp_dup_str`** — `strlen(s) + 1` could overflow. Returns
   NULL on overflow.
2. **`mpsc_queue_init`** — `capacity * sizeof(slot)` could
   overflow. Returns `OTLP_ERR_INVALID_ARGUMENT`.
3. **`otlp_exporter_create`** (resource_attributes) — explicit
   overflow check before the calloc.
4. **`normalize_opts`** — added `OTLP_MAX_BATCH_SIZE` (1M) clamp
   on `batch_size`. Prevents `batch_size * 2` from wrapping in
   the pending-array allocation.

## Allocation-site audit table

Every allocation site in `src/` has been examined:

| Site | Check | Fixed |
|---|---|---|
| metric bounds/bucket_counts | explicit overflow check | v0.5.62 |
| metric exp_pos/exp_neg counts | explicit overflow check | v0.5.62 |
| metric clone paths | defensive overflow check | v0.5.62 |
| `otlp_dup_str` len+1 | explicit overflow check | v0.5.63 |
| `mpsc_queue_init` capacity×slot | explicit overflow check | v0.5.63 |
| exporter resource_attributes | explicit overflow check | v0.5.63 |
| exporter pending arrays | batch_size clamp | v0.5.63 |
| HTTP req_buf total | already overflow-checked | pre-existing |
| protobuf buf growth | doubling with SIZE_MAX guard | pre-existing |
| platform sock struct | constant sizeof | safe by construction |
| sampler struct | constant sizeof | safe by construction |
| span struct | constant sizeof | safe by construction |

## batch_size clamp rationale

1M items per batch is ~200 MB of encoded wire data. Callers
wanting more throughput should shard across multiple exporters
rather than one giant batch. The clamp:

- Prevents `batch_size * 2 * sizeof(ptr)` from overflowing.
- Prevents enormous allocations that would fail anyway.
- Is generous enough to never affect legitimate callers.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 34/34 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

## Audit context

v0.5.62 started the integer-overflow audit (metric paths).
v0.5.63 completes it (every remaining allocation site). The
codebase now has explicit overflow checks at every
caller-supplied-size allocation — no site relies on implicit
calloc overflow detection or luck.
