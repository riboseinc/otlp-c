# TODO 146 — Open the 0.6 stabilization window (v0.6.0)

**Status:** Complete (v0.6.0)
**Priority:** P1 (milestone; follows directly from the v0.5.104 Path-to-1.0)

## What this is

The 1.0 criteria written in v0.5.104 require one full minor
cycle of additive-only API changes before the freeze. All other
criteria were already satisfied at 0.5.105, so the next release
opens that window: 0.5.105 → **0.6.0** (MINOR bump, per the
versioning contract — a compatibility commitment, not a feature
drop).

## The commitment

From 0.6.0 to 1.0.0 the public API is additive-only: new
functions, new opt-in struct fields, bug fixes. Nothing breaks,
nothing is removed. The CHANGELOG entry records the criteria
snapshot at the window's opening (surface audited + covered,
wire conformance pinned + golden-validated, boundary validation
complete, no open P1/P2) so the freeze decision later is a
check against a recorded baseline, not a re-derivation.

## Scope discipline

No library code changes in this release — the diff is the three
version constants and the documentation record. A window opener
that also changes code would blur the freeze point.

## Verification

Version constants agree (version.h 0/6/0, CMakeLists 0.6.0,
vcpkg.json 0.6.0); `otlp_version()` string derives from the
constants. Full suite re-run green at the bumped version before
the PR.
