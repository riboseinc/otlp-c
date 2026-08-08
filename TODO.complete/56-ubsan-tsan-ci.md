# TODO 56 — UBSAN + TSAN CI jobs

**Status:** Complete (v0.5.15)
**Priority:** P2

## What shipped (v0.5.15)

### Added — UndefinedBehaviorSanitizer CI

New `ubsan` job: builds with `-DOTLP_C_ENABLE_UBSAN=ON` on Ubuntu
24.04, runs full test suite with `print_stacktrace=1:abort_on_error=1`.
Catches integer overflow, null pointer dereference, alignment issues,
and other undefined behavior that ASAN doesn't detect.

### Added — ThreadSanitizer CI

New `tsan` job: builds with `-DOTLP_C_ENABLE_TSAN=ON` on Ubuntu
24.04, runs full test suite. Catches data races in the MPSC queue,
tracer PRNG (atomic CAS), and exporter stats counters. The primary
tool for validating lock-free correctness.

### Complete sanitizer coverage

| Sanitizer | Bug class | CI job |
|---|---|---|
| ASAN | Memory safety (use-after-free, overflow, leak) | ✓ |
| UBSAN | Undefined behavior (overflow, null, alignment) | ✓ |
| TSAN | Data races (lock-free correctness) | ✓ |

## Acceptance criteria
- [x] UBSAN CI job added.
- [x] TSAN CI job added.
- [x] UBSAN passes locally (27/27 tests clean).
- [x] All sanitizer build flags documented in CMakeLists.txt.
