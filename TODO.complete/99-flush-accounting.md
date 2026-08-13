# TODO 99 — Flush accounting invariant under OOM

**Status:** Complete (v0.5.59)
**Priority:** P2 (correctness: counter accounting)

## What shipped

`flush_metric` and `flush_log` incremented `emitted_*` at start
but on `pb_buf_init` failure returned without updating
`sent_*` or `dropped_*_err`. The accounting invariant
`emitted == sent + dropped_err` was violated.

Fix: on init failure, increment `dropped_*_err` before
returning. Invariant now holds across all paths.

## Sites changed

- `src/exporter.c::otlp_exporter_flush_metric` — add
  `dropped_metrics_err++` to the init-failure early return.
- `src/exporter.c::otlp_exporter_flush_log` — same for logs.
- `tests/test_allocator_oom.c` — added
  `test_flush_metric_oom_accounting` and
  `test_flush_log_oom_accounting`.

## Why this matters

Users monitoring exporter stats expect `emitted == sent +
dropped_err` to hold. Pre-v0.5.59, OOM during a sync flush
broke this invariant. A monitoring system would see "1 emitted,
0 sent, 0 dropped" and have no way to account for the missing
item — was it lost? stuck in a queue? still being processed?

Post-v0.5.59, the invariant holds. The monitoring system sees
"1 emitted, 0 sent, 1 dropped" and can act on the drop.

## Why the fail-injecting allocator caught this

The bug only manifests under OOM (rare in production). The
fail-injecting test infrastructure (introduced v0.5.56) makes
it possible to exercise OOM paths deterministically in CI.
Without it, the bug would have remained correct-by-inspection
only.

The new `test_flush_*_oom_accounting` tests verify the
invariant `emitted == sent + dropped_err` after each OOM
iteration. Any future regression that breaks the accounting
will be caught.

## Pattern continuation

This is the same "missing counter update" class as:
- v0.5.58: flush return-status omitted queue-size checks.
- v0.5.51: slab double-free path missed arena check.
- v0.5.55: resource_attributes cleanup missed zero-init.
- v0.5.56: exporter_create fail path missed mpsc_queue_free.

All are "function does X in one place but a slightly different
X in another place" bugs. v0.5.59's contribution: the init-
failure path now matches the encode-failure and POST-failure
paths in counter updates.

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

Continues the partial-init / accounting-asymmetry audit:
- v0.5.47: attribute_copy_all fail path.
- v0.5.55: resource_attributes zero-init.
- v0.5.56: mpsc_queue cleanup in exporter_create.
- v0.5.58: flush return-status queue check.
- v0.5.59: flush_metric / flush_log accounting under OOM.

The "missing X" bug class is now well-covered by the fail-
injecting allocator infrastructure. Future instances in
init paths will be caught automatically.
