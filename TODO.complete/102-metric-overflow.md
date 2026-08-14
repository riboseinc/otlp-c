# TODO 102 — Integer overflow defense in metric allocation paths

**Status:** Complete (v0.5.62)
**Priority:** P0 (memory safety: heap buffer overflow via overflow)

## What shipped

Added overflow checks before every `count * sizeof(...)` multiplication
in metric allocation paths. Without these, a caller-supplied count near
`SIZE_MAX` wraps the multiplication to a small value, producing an
undersized allocation. The subsequent `memcpy` reads/writes out of
bounds — heap buffer overflow.

## Sites changed

- `src/metric.c::otlp_metric_create` — added overflow checks for
  `histogram_n_bounds * sizeof(double)` and
  `(histogram_n_bounds + 1) * sizeof(uint64_t)`.
- `src/metric.c::otlp_metric_set_exp_histogram` — added overflow
  checks for `pos_n * sizeof(uint64_t)` and
  `neg_n * sizeof(uint64_t)`.
- `src/metric.c::otlp_metric_clone` — defensive overflow checks
  for the same fields (src was validated at create, but the checks
  are cheap).
- `tests/property/test_property_metrics.c` — added
  `prop_metric_rejects_overflow_sizes`.

## Attack vector

A malicious or buggy caller (e.g., a language VM binding that
translates from a 64-bit integer type without range checking)
passes `SIZE_MAX` as the histogram bounds count. The multiplication
wraps; malloc returns a small buffer; memcpy writes past the end.

This is CWE-190 (Integer Overflow or Wraparound) leading to
CWE-787 (Out-of-bounds Write).

## Why the clone path too

Clone's src was validated at create time. But a future code change
could add a new path that sets n_bounds without the create-time
check. The defensive overflow check in clone ensures no overflow
regardless of how src was populated.

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

Continues the audit pattern. v0.5.62 is the first integer-overflow
fix in the codebase (previous fixes were field-number, wire-type,
memory-safety, and accounting bugs). Overflow checks are standard
defensive C programming; the codebase now has them at every
caller-supplied-size allocation site.
