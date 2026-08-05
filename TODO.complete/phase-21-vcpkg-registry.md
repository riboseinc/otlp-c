# TODO 21 — vcpkg registry publication

**Status:** Out of scope (external PR)
*Closed because:* Requires PR to microsoft/vcpkg; user-driven. The overlay port in this repo is sufficient for v0.x consumers.
**Priority:** P2
**Branch:** future (post-v0.2)

## Goal

Publish otlp-c to the vcpkg registry so consumers can
`vcpkg install otlp-c` directly without overlay ports.

## Acceptance criteria

- [ ] PR opened against `microsoft/vcpkg` adding `ports/otlp-c/`.
- [ ] SHA pinned to a release tag (v0.2.0+).
- [ ] vcpkg CI passes (the registry's port-CI).
- [ ] README updated to mention `vcpkg install otlp-c`.
- [ ] Quickstart shows the vcpkg path end-to-end.

## Why

Today the README mentions vcpkg but the port doesn't exist in the
registry. Consumers have to use an overlay port from this repo,
which is friction. Being in the registry removes that.

## Tradeoff

vcpkg port fixes the version (consumers can't pick latest main).
That's fine — it's the standard pattern.
