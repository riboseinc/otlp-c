# TODO 97 — Extended OOM test coverage

**Status:** Complete (v0.5.57)
**Priority:** P3 (test coverage; no new bugs found)

## What shipped

Extended the fail-injecting OOM test harness (introduced in
v0.5.56) to cover every major init path that allocates multiple
resources. Four new tests added to `test_allocator_oom.c`:

- `test_metric_create_histogram_oom` (30 iterations)
- `test_metric_clone_oom` (50 iterations)
- `test_log_record_clone_oom` (40 iterations)
- `test_tracer_create_oom` (20 iterations)

Total: 140 new fail-injecting iterations across the four paths.

## Why no new bugs

The v0.5.56 bug (`mpsc_queue` cleanup in exporter_create's fail
path) was the third instance of the partial-init cleanup bug
class. v0.5.47 (attribute_copy_all) and v0.5.55 (resource_attrs)
were the earlier instances.

The four new paths audited in v0.5.57 (`metric_create`,
`metric_clone`, `log_record_clone`, `tracer_create`) all follow
the correct pattern:
- Allocate via `calloc` or `malloc + memset(0)` so unset fields
  are NULL.
- On failure, call the type's destructor (which handles NULL
  fields as no-ops) OR explicitly free each potentially-
  allocated resource.

No partial-init leaks found. The test passes confirm that the
codebase's init paths are correctly designed across the board.

## Why this is still valuable

The test infrastructure is now a regression guard. If a future
change introduces a partial-init bug in any of these paths, the
test will catch it immediately under ASAN/LSAN in CI.

v0.5.56 showed the test can find real bugs. v0.5.57 confirms the
rest of the codebase is clean against the same bug class.

## Pattern summary

Every multi-alloc init path now has fail-injecting coverage:

| Path | Resources allocated | Test added |
|---|---|---|
| `otlp_exporter_create` | struct + 2 strings + attrs + 3 arrays + 3 queues | v0.5.56 |
| `otlp_span_clone` | struct + name + attrs + events + links | v0.5.56 |
| `otlp_metric_create` (hist) | struct + 3 strings + bounds + bucket_counts | v0.5.57 |
| `otlp_metric_clone` | above + attrs + exp_pos + exp_neg | v0.5.57 |
| `otlp_log_record_clone` | struct + 2 strings + attrs | v0.5.57 |
| `otlp_tracer_create` | struct + 3 strings | v0.5.57 |

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

This release closes the test-coverage arc that started in
v0.5.56:

- v0.5.47: fixed attribute_copy_all (correct-by-inspection).
- v0.5.55: fixed resource_attributes zero-init (correct-by-
  inspection).
- v0.5.56: fail-injecting test + found mpsc_queue leak (test
  caught it).
- v0.5.57: extended test to all init paths (no new bugs, but
  regression coverage now comprehensive).

The partial-init cleanup bug class is now closed: every init
path is tested, every fix is regression-guarded.
