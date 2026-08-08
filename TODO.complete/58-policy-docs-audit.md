# TODO 58 — Policy-docs staleness audit

**Status:** Complete (v0.5.19)
**Priority:** P1

## What shipped (v0.5.19)

The policy documents — `SECURITY.md`, `SECURITY-ASSESSMENT.md`,
`README.md` — had drifted out of sync with the v0.5.x codebase.
This is the same kind of audit [[57-claude-md-audit]] did for
CLAUDE.md, applied to the rest of the policy surface.

### SECURITY.md — 2 fixes

1. **Line 32 stale claim**: "Race conditions in the exporter's
   **background thread**" — the library has had no background thread
   since the caller-tick exporter landed in early v0.5.x. The
   actual concurrency surface is the MPSC queue and atomic stats.
   Replaced with the correct surface (MPSC queue, atomic counters,
   tracer PRNG).
2. **Line 55 hardening section**: recommended ASAN + UBSAN but
   omitted TSAN. The CI runs all three (added in v0.5.15). Added
   `-DOTLP_C_ENABLE_TSAN=ON` to the recommendation.

### SECURITY-ASSESSMENT.md — scope refresh v0.1.x → v0.5.x

The assessment was tagged "Security review — v0.1.x" but the
project is at v0.5.x. Three drifts:

1. **Surface section**: did not cover metrics, logs, context
   propagation, sampler, slab allocator — all added since v0.1.x.
   Added a Surface addendum covering each new module's input
   boundary and NULL/overflow behavior.
2. **Slab-as-global-allocator threat-model note**: a new
   security-relevant surface — `otlp_install_slab_allocator` hooks
   `malloc`/`free` process-wide. Documented the threat (a hostile
   caller installing the slab then freeing a non-slab pointer is
   caught by the address-range check; the slab never accepts an
   arbitrary pointer from outside its arena).
3. **Recommendations section**: items in "Recommendations for
   v0.2.x" had been completed in subsequent releases but were
   still listed as open. Marked each with a status line ("Done:
   TODO.complete/XX-...") so future readers see at a glance what's
   resolved.

### README.md — 3 fixes

1. **Line 23 version**: "**0.5.10.**" → "**0.5.17.**" (we are at
   v0.5.17 as of this PR; release workflow bumps to 0.5.18 on
   merge).
2. **Line 5 badge URL**: pointed at `workflows/build.yml` which
   was renamed to `workflows/ci.yml` in an earlier release. Badge
   was broken (404 on the badge SVG). Fixed to `workflows/ci.yml`.
3. **Line 165 BSD coverage**: claimed "OpenBSD, NetBSD" alongside
   Linux/macOS/Windows. CI only covers Linux, macOS, FreeBSD
   (best-effort), Windows. OpenBSD/NetBSD are POSIX-compliant and
   expected to work, but not CI'd. Reworded to distinguish
   "CI'd" from "expected to work on any POSIX platform".

CONTRIBUTING.md and CODE_OF_CONDUCT.md reviewed — both current,
no changes needed.

## Why this matters

Policy docs are the canonical answer to "what is this project?"
Stale claims propagate:

- The "background thread" line in SECURITY.md would tell a future
  security reviewer to look for the wrong concurrency model.
- The broken README badge told visitors the build was failing (red
  badge SVG because the workflow file was missing) — first
  impression damage.
- The overconfident BSD claim is a soft integrity issue: a claim
  of CI'd platforms that aren't actually CI'd erodes trust in
  every other claim the project makes.

This is the second-pass accuracy audit; CLAUDE.md (TODO 57) was
the first. Together they cover every doc a contributor or
evaluator reads first.

## Acceptance criteria
- [x] No "background thread" claim in any policy doc.
- [x] All three sanitizers recommended in SECURITY.md hardening.
- [x] SECURITY-ASSESSMENT.md scope reflects v0.5.x surface.
- [x] README version string matches `version.h`.
- [x] README CI badge URL resolves (workflows/ci.yml exists).
- [x] README platform coverage distinguishes CI'd vs expected.
- [x] All 27 tests still pass.
