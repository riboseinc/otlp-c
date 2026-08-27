# TODO 186 — seventh review: the soak mechanized (v1.1.11)

**Status:** Complete (v1.1.11)
**Priority:** P2 (the last manual discipline)

## What was wrong

1. The 100k property soak was a documented protocol that ran
   when a human remembered: 21 CPU-side binaries at 100k
   iterations, 4 socket/timing binaries excluded (their default
   1000 IS their soak — each iteration sleeps real
   wall-clock). Memory-enforced, like the docs sync and the
   perf claims before their gates.
2. The error-taxonomy audit (first ever) found the enum sound
   but two Phase 0 relics lying: NOT_IMPLEMENTED's "Placeholder
   for unimplemented code" comment (no path returns it since
   v0.5) and test_smoke.c's stub narration.

## The fix

- `.github/workflows/soak.yml`: weekly cron (Sun 03:00 UTC) +
  workflow_dispatch. Runs the property binaries DIRECTLY —
  ctest's per-test TIMEOUT 60 predates the soak flow and
  --timeout cannot override a set TIMEOUT property. Failure
  opens an issue (label ci) with the run URL and the
  OTLP_C_PROPERTY_SEED replay recipe. Loop logic validated
  locally before shipping: exactly 21 soaked / 4 skipped, all
  green.
- Two comment fixes; the frozen enum value stays (strerror
  completeness pin covers it).

## Lesson

Review the COMMENTS against the invariants, not just the code:
a comment can freeze a lie in place for 180 releases while the
code moved on.
