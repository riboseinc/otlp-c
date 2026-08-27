# TODO 185 — sixth review: the perf truth (v1.1.10)

**Status:** Complete (v1.1.10)
**Priority:** P2 (docs truth + a real CI gate)

## What was wrong

Performance was the last memory-enforced claim surface:

1. The site perf page carried unversioned numbers, and one was
   mis-attributed: "~150 ns per span at 5 attributes" was
   always the 0-attr figure (v0.5.76-era). 5 attrs costs ~540
   ns today. The encode figures (360 ns @4 attrs, ~60 ns/attr)
   survived re-measurement (~380, ~58) — emit did not.
2. The page claimed "every release re-verifies the pipeline
   numbers before merging" — a discipline held in memory, with
   no CI job running bench at all.

## The fix

- Perf page re-stamped: every figure carries the release and
  date it was measured at (v1.1.9 bench preset, Apple silicon);
  sizeof(otlp_span) attributed to its v0.5.76 measurement (the
  struct is opaque by design and its layout is frozen).
- bench-smoke CI job: builds the bench suite, runs
  otlp_bench_emit, asserts the 0-attr emit figure under a
  3000 ns/span ceiling (~20× the median) — catches algorithmic
  blowups, tolerates shared-runner noise. Gate logic verified
  end-to-end locally (parsed 145.0 → pass).

## Lesson

A number without a version stamp is a future lie — same class
as the v1.1.5 tag pins and the v1.1.4 counts. Perf claims now
follow the site-docs sync discipline: dated, versioned, and
backed by a job.
