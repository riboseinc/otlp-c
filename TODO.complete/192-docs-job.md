# TODO 192 — thirteenth review: the docs PR job (v1.1.17)

**Status:** Complete (v1.1.17)
**Priority:** P2 (the newest gates were main-only)

## What was wrong

The Doxygen WARN_AS_ERROR gate (v1.1.13) and the Astro site
build ran ONLY in the pages job (if: push && main). A docstring
regression or a broken site page passed every PR check and
broke main's deploy after merge — the exact failure shape of
the FreeBSD continue-on-error mask: green PRs, broken main.

## The fix

- New `docs` job on every PR: cmake docs target (Doxygen
  WARN_AS_ERROR enforced) + npm ci + astro build. Pages keeps
  main-only deployment.
- bench-smoke gains the encode pipeline (5000 ns/span ceiling
  at 1 attr; the awk field lesson repeated — $10 not $6 —
  caught by dry-running the parse locally before shipping).

## Feature-sized candidate, NOT taken

A 4th signal (profiles) is the next module-shape change per
ADR 0006 — additive, one SIGNAL_SPECS row — but upstream
profiles are experimental and our descriptors pin
opentelemetry-proto 1.44.0. Starting it is a product decision
(user-gated, like TLS/2.x).
