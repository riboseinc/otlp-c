# TODO 82 — Clone-variant emit shutdown-before-alloc symmetry

**Status:** Complete (v0.5.42)
**Priority:** P3 (performance + symmetry)

## What shipped

`otlp_exporter_emit` (span clone variant) has always checked
`shutdown_requested` BEFORE cloning. The metric/log clone variants
cloned FIRST, then delegated to the move variant which freed the
clone on shutdown (post-v0.5.41). The cycle was correct but wasted
an alloc+free under shutdown contention.

This release brings `emit_metric` and `emit_log` into symmetry with
`emit`: shutdown check happens BEFORE the clone.

## Sites changed

- `src/exporter.c::otlp_exporter_emit_metric` — added
  `otlp_atomic_load_int(&e->shutdown_requested, ...)` check before
  `otlp_metric_clone`.
- `src/exporter.c::otlp_exporter_emit_log` — same.

## Why this matters

For metrics with many attributes / exponential histogram buckets /
bounded-explicit arrays, `otlp_metric_clone` is non-trivial work.
Calling it under shutdown — only to free the result immediately —
is pure waste. The fix avoids the alloc+free entirely.

The semantic win is consistency: all three clone-variant emits now
have identical shape (NULL check → shutdown check → clone → move).

## Test gap (and why it's narrow)

The clone-variant shutdown properties
(`prop_async_metric_clone_shutdown`, `prop_async_log_clone_shutdown`)
verify SHUTDOWN return. They don't directly verify "no allocation
happened" — that would require intercepting the allocator (still
pending, see TODO 28).

External behavior is identical with or without the fix because the
move variant frees the clone correctly. The properties exist to
lock in the contract that all clone variants return SHUTDOWN
promptly under shutdown, regardless of internal implementation.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build -E http-timeout   # 39/39 pass
                                       # http-timeout is a known
                                       # macOS-local flake; passes
                                       # in CI on Linux.
cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout  # ASAN clean
```
