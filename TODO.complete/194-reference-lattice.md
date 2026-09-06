# TODO 194 — fifteenth review: the reference lattice (v1.1.19)

**Status:** Complete (v1.1.19)
**Priority:** P3 (gate completion)

## What was wrong

1. The load-bearing docs name files with nothing checking
   existence: CLAUDE.md key-files (the map every future agent
   starts from — a stale path misdirects automation, not just
   readers), the ADR index, architecture.md relative links.
2. The soak path (ctest -L soak) only ran on the weekly cron —
   a label regression or broken selection surfaced Sunday
   03:00 UTC, not at the causing PR.

## The fix

- include_lint.py gains the lattice section: brace-expansion-
  aware path checks for CLAUDE.md key-files, ADR-index
  completeness, architecture.md link existence. Clean at gate
  time; two new self-test mutations defend it (11 total).
- The bench-smoke job becomes "Bench + soak smoke": builds
  tests, runs OTLP_C_PROPERTY_ITERS=10000 ctest -L soak per PR
  (21/21 in ~30s locally); 100k stays on the cron.

## Note

The lattice probe's first run reported three "missing" paths —
all brace-expansion notation (src/foo.{h,c}), not real gaps.
A checker must understand the notation or it false-positives
forever; expanded, everything exists.
