# TODO 161 — The vcpkg overlay port made real and tested

**Status:** Complete (v0.6.15)
**Priority:** P1 (shipped packaging artifact was broken)

## The rot

`ports/otlp-c/` — the in-repo overlay port — had never been
built once:

- `vcpkg_from_github` pinned at **v0.5.17** (97 releases stale)
  with an **all-zeros placeholder SHA512**
- missing the **vcpkg-cmake-config** host dependency, so
  `vcpkg_cmake_config_fixup` was an unknown command — the recipe
  failed the moment anything ran it
- `ports/otlp-c/vcpkg.json` version frozen at 0.5.17 for a
  hundred releases

Found by doing what v0.6.9 did for FetchContent: actually run the
documented path.

## What shipped

- **Portfile builds the LOCAL checkout**
  (`SOURCE_PATH = ${CURRENT_PORT_DIR}/../..`): no REF/SHA to
  maintain, so the recipe cannot drift behind releases. The
  upstream-submission variant (vcpkg_from_github + REF/SHA) is
  documented in the portfile comment for the day a port is
  actually submitted.
- vcpkg-cmake-config host dependency added; port manifest version
  tracks the library.
- **`tests/consumers/vcpkg_overlay/`** — manifest consumer
  (vcpkg.json → otlp-c) + round-trip program; validated locally
  end-to-end with a real vcpkg checkout (clone + bootstrap +
  overlay install + link + run), including the nested-inside-the-
  repo manifest-discovery case.
- **CI job "vcpkg overlay consumer" (ubuntu)** — bootstraps vcpkg
  and runs the consumer; the third consumption path is pinned
  like find_package (v0.5.x era) and FetchContent (v0.6.9).
- Docs drift swept in passing: quickstart FetchContent tag
  v0.6.8 → v0.6.15; README vcpkg section now shows the working
  overlay recipe; CLAUDE.md key-files gains http_response_parser
  and retry_policy; the architecture layer diagram shows both.

## Verification

- Local: overlay install + consumer build + emit-to-flush round
  trip PASS against a fresh vcpkg (2026-07-27 toolchain), both
  standalone and nested in the repo.
- 51/51 tests; Doxygen zero warnings; ci.yml YAML valid.
