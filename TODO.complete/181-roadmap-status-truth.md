# TODO 181 — roadmap truth, status edition (v1.1.6)

**Status:** Complete (v1.1.6)
**Priority:** P3 (documentation truth)

## What was wrong

The roadmap's *status layer* (the phase table, the out-of-scope
table, one section heading) had frozen at old truths while the
release tables beneath it kept moving:

- Phase 20 "Windows MSVC fix" — "Deferred (MSVC team)". MSVC
  x64/ARM64 CI jobs have been green for 100+ releases.
- "## v0.4 (current)" — a "(current)" marker from the 0.4 era,
  five majors of drift.
- Out-of-scope row #11 "FreeBSD CI — Best-effort …
  continue-on-error" — directly contradicted the v1.1.3 row in
  the same file (gating check since).
- README's platform line carried the same "(best-effort)"
  FreeBSD claim.

## The fix

Phase 20 → Done; "(current)" dropped from the v0.4 heading; the
FreeBSD out-of-scope row removed (history preserved by the
v1.1.3 release row + TODO 178); README says FreeBSD gates with
the full suite. Site changelog carries 1.1.6 in the same release
(the TODO 180 lesson, applied).

## Lesson

A status field is a claim about *now*; it rots faster than
prose. When a status flips (deferred → done, best-effort →
gate), every table asserting the old status is a live lie:
grep for the old word ("Deferred", "best-effort", "(current)")
when the state changes.
