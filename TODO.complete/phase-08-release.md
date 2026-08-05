# TODO 08 — Tag 0.1.0

**Status:** Complete
**Phase:** 8
**Priority:** P0
**Branch:** `phase-8-release`

## Goal

Version constants aligned across all sources; CHANGELOG populated; release workflow added. **The `v0.1.0` git tag is created by the user, never by the agent** (per global rule).

## Acceptance criteria

- [ ] `include/otlp-c/version.h` reports `0.1.0`.
- [ ] `CMakeLists.txt` `project(otlp-c VERSION 0.1.0)`.
- [ ] `vcpkg.json` `"version-string": "0.1.0"`.
- [ ] `otlp_version()` runtime returns `"0.1.0"`.
- [ ] `CHANGELOG.md` summarizes everything shipped.
- [ ] `.github/workflows/release.yml` builds artifacts on tag push.
- [ ] All phases 1-7 merged.
- [ ] Full CI matrix green.
- [ ] `ctest --test-dir build` 100% pass (unit + property + integration).

## Files

- `include/otlp-c/version.h` — verify.
- `CMakeLists.txt` — verify.
- `vcpkg.json` — verify.
- `CHANGELOG.md` — new.
- `.github/workflows/release.yml` — new.
- `README.md` — add "Status: 0.1.0 — alpha" banner.

## Post-merge (user action only)

```
git checkout main && git pull
git tag -a v0.1.0 -m "otlp-c 0.1.0"
git push origin v0.1.0
```

The agent never executes the above. The release workflow then builds artifacts.

## Dependencies

- Phases 1-7.

## Completion evidence

All three version sources agree on `0.1.0`:
- `include/otlp-c/version.h`: `OTLP_C_VERSION_MAJOR 0`, `_MINOR 1`, `_PATCH 0`, `_STRING "0.1.0"`.
- `CMakeLists.txt`: `project(otlp-c VERSION 0.1.0 ...)`.
- `vcpkg.json`: `"version": "0.1.0"`.
- `otlp_version()` runtime returns `"0.1.0"` (smoke test asserts this).

`CHANGELOG.md` written. `README.md` "Status" section updated to
"0.1.0 (alpha)" with phase summary.

The actual `v0.1.0` git tag is the user's action per global rule
("NEVER push git tags").
