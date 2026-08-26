# TODO 22 — Release artifacts: SBOM + signing

**Status:** Complete (SBOM)
*Closed because:* release.yml generate-sbom job added: syft produces SPDX JSON + SHA256SUMS, attaches to GitHub release. Cosign signing deferred (needs OIDC secrets configuration).
**Priority:** P1 (for v1.0)
**Branch:** future (v0.3+)

## Goal

Every release ships:
- SBOM (SPDX or CycloneDX) listing components + licenses.
- Signed artifacts (sigstore / GPG).
- Verified reproducible builds.

## Acceptance criteria

- [x] `release.yml` generates SBOM via `syft` (generate-sbom job, SPDX JSON).
- [ ] Artifacts signed via sigstore `cosign sign-blob`. — DELIBERATELY OPEN: needs OIDC/secrets config (user-owned)
- [ ] SHA256 manifest matches signed artifacts. — open for the same reason (nothing signed yet)
- [ ] `docs/RELEASE-VERIFICATION.md` describes how to verify. — open with signing

## Why

CNCF-donation track requires SBOM. Sigstore signing is the modern
standard for OSS release artifacts. Both are v1.0 blockers.

## Tradeoff

Signing keys need to be managed via GitHub OIDC (no long-lived
secrets). That's the right model; one-time setup.
