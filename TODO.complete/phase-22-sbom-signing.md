# TODO 22 — Release artifacts: SBOM + signing

**Status:** Pending
**Priority:** P1 (for v1.0)
**Branch:** future (v0.3+)

## Goal

Every release ships:
- SBOM (SPDX or CycloneDX) listing components + licenses.
- Signed artifacts (sigstore / GPG).
- Verified reproducible builds.

## Acceptance criteria

- [ ] `release.yml` `build-artifacts` job generates SBOM via `syft` or equivalent.
- [ ] Artifacts signed via sigstore `cosign sign-blob`.
- [ ] SHA256 manifest already present (added in earlier work) — verify it matches signed artifacts.
- [ ] `docs/RELEASE-VERIFICATION.md` describes how to verify.

## Why

CNCF-donation track requires SBOM. Sigstore signing is the modern
standard for OSS release artifacts. Both are v1.0 blockers.

## Tradeoff

Signing keys need to be managed via GitHub OIDC (no long-lived
secrets). That's the right model; one-time setup.
