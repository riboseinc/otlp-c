# TODO 15 — Doxygen API reference + cookbook

**Status:** Pending
**Priority:** P1
**Branch:** future (v0.2.x)

## Goal

Generate API reference from header doccomments via Doxygen. Add a
cookbook of embedding patterns (libuv, epoll, IOCP, Node addon,
Python C extension, game loop, firmware main loop).

## Acceptance criteria

- [ ] `Doxyfile` configured for the project.
- [ ] All public headers have `/** */` doccomments on every declaration.
- [ ] `cmake --build build --target docs` produces `build/docs/html/`.
- [ ] `docs/cookbook.md` with at least 5 working patterns.
- [ ] GitHub Pages action publishes docs on push to main (under `gh-pages` branch).

## Files

- `Doxyfile` — new.
- `docs/cookbook.md` — new.
- `docs/api-reference.md` (already added in Phase 7 placeholder) — wire to Doxygen output.
- `include/otlp-c/*.h` — fill in any missing doccomments.
- `CMakeLists.txt` — docs target.
- `.github/workflows/docs.yml` — new (Pages deploy).

## Why

A stranger landing on the repo should reach "I have a working span
in 5 minutes" via the quickstart (✓ Phase 7), and "I know exactly
what every function does" via Doxygen. The cookbook answers "how do
I integrate into MY event loop?"
