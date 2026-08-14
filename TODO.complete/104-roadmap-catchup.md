# TODO 104 — Roadmap + CLAUDE.md catch-up (27 releases)

**Status:** Complete (v0.5.64)
**Priority:** P2 (documentation; project tracking)

## What shipped

The roadmap (`docs/roadmap.md`) stopped at v0.5.36 — 27 releases
undocumented. `CLAUDE.md` claimed "All phases are complete
(v0.5.35)" — 28 releases behind.

Both are now current through v0.5.63.

## Sites changed

- `docs/roadmap.md` — added "v0.5.37–v0.5.63 — deep audit arc"
  section with all 27 releases, their PR numbers, and a
  bug-class coverage summary.
- `CLAUDE.md` — updated version reference; added mentions of
  the descriptor-driven dispatch (v0.5.43-46), schema fixes
  (v0.5.48/49/61), header-injection hardening (v0.5.52/53),
  ID validation (v0.5.54), RFC 7230 parser fix (v0.5.47),
  fail-injecting allocator (v0.5.56/57), accounting invariant
  (v0.5.59), and integer-overflow defense (v0.5.62/63).

## Why this matters

The roadmap and CLAUDE.md are the first things a new contributor
reads. Both claimed "34 tests, 7 bugs fixed" — missing 28
releases of work. A contributor would drastically underestimate
the project's actual maturity (34 tests, **34+ bugs fixed**).

## The audit arc in numbers (v0.5.37-v0.5.63)

- 27 releases, all PR-based with CI.
- 34+ distinct bugs found and fixed.
- 5 architectural refactors (table-driven dispatch).
- 2 test infrastructure releases.
- 3 documentation releases.
- Bug classes: wire format (10), W3C (3), memory safety (4),
  security (4), accounting (3), integer overflow (8+).

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 34/34 pass
```
