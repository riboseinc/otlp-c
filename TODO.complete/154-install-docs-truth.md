# TODO 154 — Install-docs truth: vcpkg port claim + stale release-notes dir

**Status:** Complete (v0.6.8)
**Priority:** P3 (docs truth)

## The lies

1. **README.md and docs/quickstart.md told consumers to install
   `otlp-c` from vcpkg** — `"dependencies": ["otlp-c"]` in the
   user's `vcpkg.json`. Verified against the registry: no
   `otlp-c` port exists in `microsoft/vcpkg` master (404 on
   `ports/otlp-c/vcpkg.json`), and Ribose publishes no registry
   fork (`riboseinc/vcpkg` — 404). Every user who followed the
   instructions verbatim hit an unresolved-dependency error.
   The repo's own `vcpkg.json` is a manifest for *building*
   otlp-c under a vcpkg toolchain (it deliberately declares
   zero dependencies) — it is not, and does not create, an
   installable port.

2. **docs/release-notes/ held exactly one file** (v0.5.0.md,
   2026-08-07) while 100+ releases shipped after it, and nothing
   in the repo links to the directory. Canonical per-release
   notes are CHANGELOG.md + GitHub Releases.

## What shipped

- README + quickstart now give the real consumption paths:
  CMake FetchContent against a release tag, `add_subdirectory()`
  of a clone/submodule, or `cmake --install` +
  `find_package(otlp-c CONFIG)`. The vcpkg section is reframed
  as "toolchain environment" — building this repo under a vcpkg
  toolchain — and says plainly that no port is published.
- `docs/release-notes/README.md` marks the directory frozen at
  v0.5.0 and points at CHANGELOG.md + GitHub Releases as the
  canonical notes. v0.5.0.md kept for the record (source files
  are never deleted).

## Also fixed in this release (found during validation)

- **property-http-timeout failed deterministically on VPN
  networks**: the property connects to TEST-NET-1 expecting the
  connect to hang or be refused; VPN/proxy stacks locally accept
  every TCP connect, so the request reached READING and
  terminated at the read deadline — which equaled the test's own
  5s cap (observed 5009 ms). Read deadline now 2000 ms; the
  property documents and accepts all three bounded outcomes.
  The library was correct throughout.
- **roadmap.md's v0.6.5 row contained a literal `\ref`** ("a
  wall of \ref warnings") that Doxygen parsed as a command —
  the one warning in an otherwise-clean docs build. Reworded.

## Verification

- Full sweep for remaining consumer-vcpkg claims: none.
- Doxygen builds with zero warnings (both edited pages are in
  INPUT). 49/49 tests in Debug and Release; fresh Release tree
  zero warnings.
