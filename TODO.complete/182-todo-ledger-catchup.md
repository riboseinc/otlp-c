# TODO 182 — the TODO ledger catches up with itself (v1.1.7)

**Status:** Complete (v1.1.7)
**Priority:** P3 (documentation truth — the ledger's own)

## What was wrong

42 files in TODO.complete/ carried **Status: Complete** with
212 unticked acceptance boxes. Every file maps to a merged
release — the work shipped; the paperwork never did. The
ledger's ticks are what a future auditor greps; an unticked
box under a Complete banner is a small lie either way.

## What was done

- Blanket tick on 36 code/history-verifiable files (194 boxes).
- Targeted: TODO 11 (FreeBSD WONTFIX reframed — v1.1.3 proved
  the quirk fixable), TODO 09 (stale "Windows is
  continue-on-error" closed-because), TODO 45 (tag step — done
  every release since), TODO 50 (the mock shipped as
  null_transport; no continue-on-error remains in CI),
  phase-12 ("(partial)" dropped — all five shipped),
  phase-20 (windows continue-on-error gone),
  phase-22 (SBOM ticked; signing annotated OPEN).
- Deliberately left unticked (35 boxes, 7 files): TLS / gRPC
  (v1.x WONTFIX), out-of-scope items, phase-14 arena (work
  shipped then was removed — ticked would imply present),
  phase-21 vcpkg externals (user-owned), phase-22 signing
  (user-owned), TODO 44's v0.2 social actions (unverifiable
  from the repo).

## Lesson

The tick is a claim about a specific criterion, not a mood.
When a phase closes, tick in the closing commit — or annotate
honestly why not. A ledger where Status and ticks disagree is
two ledgers.
