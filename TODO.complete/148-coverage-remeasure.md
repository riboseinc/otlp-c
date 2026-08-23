# TODO 148 — Coverage re-measurement + deep-clone round-trips

**Status:** Complete (v0.6.2)
**Priority:** P2 (the roadmap's 82%-every-file bar had regressed)

## Measurement

Re-ran the clang-profile coverage flow (`OTLP_C_ENABLE_COVERAGE`,
LLVM_PROFILE_FILE + xcrun llvm-profdata/llvm-cov — note:
Homebrew llvm's tools reject Apple Clang's v8 profraw format;
use `xcrun`). Merged across all 48 test binaries, five files had
drifted below the documented 82% line-coverage bar:

| file | before | after |
|---|---|---|
| platform.c | 71.4% | 93%+ |
| span.c | 79.7% | 84%+ |
| internal_util.c | 81.0% | 84.5% |
| otlp_metrics_encoder.c | 81.1% | 82.1% |
| metric.c | 81.3% | 83%+ |

Root cause: the clone/copy arms for anything richer than scalar
attributes had NEVER executed — histogram bounds + bucket_counts
and exp-histogram buckets in `otlp_metric_clone`, status_message /
trace_state in `otlp_span_clone`, bytes/array/kvlist copies in the
attribute-copy switch, and the event/link attribute setters
entirely (events were only ever created bare in tests). The
v0.5.92 baseline predated most of that code.

## What shipped

**`tests/unit/test_unit_clone.c`** (4 tests):
- span deep clone: status + trace_state + every attribute type on
  span/event/link (including the array/kvlist setters on both),
  proven by byte-equal encoding of original vs clone, freed both
  (ASAN owns the copy/free symmetry)
- metric deep clone: histogram with bounds + records, exp
  histogram with explicit bucket arrays + composite attributes —
  byte-equal encoding before/after
- log deep clone: severity, trace correlation, composites
- the has_start/has_time emission matrix for NDP and HDP
  (neither/start-only/time-only/both) + free(NULL) guards

**`test_unit_platform.c`**: clock NULL-guard + sanity tests
(otlp_platform_now_{unix,mono}_nano).

Accepted as out of scope (documented): the OOM-propagation arms
in the encoders (~60 lines in otlp_metrics_encoder.c) — covering
them needs fail-injection into the ENCODE paths; the existing
allocator-oom harness injects at exporter_create/span_clone.

## Verification

49/49 tests green in all five configurations; fresh Release tree
zero warnings; every library file back at 82%+ line coverage.
