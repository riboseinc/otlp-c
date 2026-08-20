# TODO 131 — Coverage lap 2: platform + tracer error paths

**Status:** Complete (v0.5.91)
**Priority:** P2 (coverage-guided: the two remaining sub-82% files)

## What shipped

Follow-up to the v0.5.90 baseline table. `llvm-cov show` mapped
the zero-hit lines in the two worst files:

- `platform_unix.c` (76%): NULL guards, DNS failure, connection
  refusal, write/read error returns.
- `tracer.c` (81%): NULL/edge paths — `free(NULL)`,
  `set_sampler(NULL, …)`, `start_child_span(NULL parent)`,
  child-trace-inheritance.

New `tests/unit/test_unit_platform.c` (4 tests):

- socket NULL guards for every platform entry point
- **DNS failure**: `nonexistent.invalid` (RFC 2606-reserved —
  resolution always fails) → `OTLP_ERR_DNS`/`OTLP_ERR_CONNECT`
- **connection refused**: bind → getsockname → close → connect to
  the now-closed loopback port → `OTLP_ERR_CONNECT` (drives the
  non-blocking finish_connect error path)
- tracer edges incl. child-span trace-id inheritance and
  has_parent

Result: platform_unix **76% → 82%**, tracer **81% → 91%** (100%
functions). Remaining misses are allocation-failure and platform
error branches not deterministically reachable.

All checks are explicit rc comparisons — no side-effecting
asserts (the Release/NDEBUG rule; the first draft produced
unused-variable warnings under Release, which is exactly how that
class of bug announces itself).

## Verification

Debug + Release + ASAN/LSAN suites green (39 tests).
