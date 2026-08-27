# TODO 184 — fifth review: the drift gates (v1.1.9)

**Status:** Complete (v1.1.9)
**Priority:** P2 (CI truth — the last memory-enforced surfaces)

## What was wrong

Two surfaces ran on memory alone:

1. `examples/` was compiled by no CI job. The three demo
   programs — the first thing strangers read — could rot
   silently against any API change. v0.5.105 already paid this
   once (the event-loop example was never built for 100+
   releases until then).
2. Four hand-maintained copies of machine-knowable facts: the
   version quartet (bumped by hand every release), the site
   changelog's newest entry vs CHANGELOG's vs version.h (the
   TODO 180 lesson — it drifted within one release), and the
   site's env-var island vs env_config.c's getenv table.

## The fix

- New `examples` CI job: configure EXAMPLES=ON, build — the
  compile is the gate. 31 checks now.
- New `tests/site_docs_sync.py` in conformance-gates: quartet
  equality, changelog↔site-changelog↔version coherence, env-var
  parity (both directions). Fails with a printed diff list.

ADR 0006's declined axes were not re-litigated; artifact
signing remains user-gated (keyless OIDC is workflow-only, but
the trust call is the maintainer's).

## Proof the gate works

This release bumped the quartet and both changelogs in one
commit; site_docs_sync.py verified coherence at 1.1.9 before
the PR was cut. Miss any of the five surfaces and the gate
goes red — the TODO 180 lesson is now mechanical.
