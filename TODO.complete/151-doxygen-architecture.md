# TODO 151 — Spec hygiene: Doxygen-clean build + architecture catch-up

**Status:** Complete (v0.6.5)
**Priority:** P3 (spec surfaces)

## What shipped

**Doxygen build made warning-free and drift-proof**:

- The hand-maintained `Doxyfile` said `PROJECT_NUMBER = 0.2.0` —
  stale for over a hundred releases. It is now `Doxyfile.in`,
  configured by CMake with `PROJECT_VERSION` (0.6.4 today), so
  the number can never go stale again.
- INPUT covered only the headers + README + cookbook; every
  README markdown link to another .doc was auto-converted by
  Doxygen to an unresolvable `\ref` (a wall of warnings). INPUT
  now includes all the spec pages — quickstart, deployment,
  otlp-spec, architecture, cookbook, roadmap, integration-test,
  plus SECURITY/CONTRIBUTING/CODE_OF_CONDUCT/CLAUDE — and the
  API reference builds with ZERO warnings at 84 HTML pages (the
  specs render as Doxygen pages now, cross-linked with the API).
- One real doc bug found via the warnings: roadmap.md contained
  `#repr(C)`, which Doxygen read as an explicit link request —
  reworded.

**architecture.md catch-up**: the module table predated
`protobuf_decode.c` and the v0.5.100/103 designs. Added its row
plus a short section on the diagnostics event model (one model,
derived string view), the UTF-8 boundary contract, and the two
independent wire-conformance tests (pins + golden vectors).

## Verification

`cmake --build build --target docs`: zero warnings, 84 pages,
version 0.6.4. Full suite green (49/49); fresh Release tree zero
warnings.
