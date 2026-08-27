# TODO 188 — ninth review: the last literals (v1.1.13)

**Status:** Complete (v1.1.13)
**Priority:** P3 (gate completion)

## What was wrong

1. README and quickstart both showed `GIT_TAG v1.1.4` as "the
   latest" — 8 releases stale, the exact rot class the v1.1.9
   sync gate exists for, on the one surface it didn't cover.
2. Doxygen was made warning-free in v0.6.5 with nothing
   keeping it that way — warnings scroll by in the Pages build.

## The fix

- site_docs_sync.py parses the GIT_TAG examples in both files
  and requires them to equal the release version (proved
  itself immediately: it caught the 8-release staleness, then
  the mid-bump state, then passed at 1.1.13).
- Doxyfile.in gains WARN_AS_ERROR = YES; the docs target
  builds clean under it.

## Declined

A tests/-level otlp_add_property_test analog: 27 blocks but
genuinely varied (per-test POSIX guards, echo sources, stress
structures) — the helper would be as wide as what it replaces
(ADR 0006 deletion test).
