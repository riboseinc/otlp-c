# TODO 127 — Documentation catch-up for the audit arc

**Status:** Complete (v0.5.87)
**Priority:** P2 (accuracy: 8 releases of findings undocumented in the roadmap)

## What shipped

Docs-only catch-up for v0.5.79–v0.5.86 (the audit arc), the same
treatment v0.5.77 gave the attribute arc:

- `docs/roadmap.md` — version-table rows for all eight releases
  (PR numbers verified via `gh pr list`, not guessed — the v0.5.77
  lesson); key-metrics block refreshed (126 TODOs, 38 tests, 40+
  bugs, Debug AND Release verification, encode ~990 ns/span @5
  attrs); new "Audit arc" summary paragraph naming the two banked
  test-discipline lessons (macOS leak detection; NDEBUG assert
  elision).
- `CLAUDE.md` — "All phases complete" claim bumped v0.5.63 →
  v0.5.86; implementing-agent capability list gained the four
  audit-arc systems (hardened HTTP client, W3C-exact propagation,
  jittered retry with shift-clamped exponent, arena-aware slab
  realloc); new **test-writing rules** section distills the three
  paid-for lessons into policy (no side-effecting asserts; verify
  Debug AND Release with wall-clock-bounded loops; macOS ASAN
  needs `detect_leaks=1`).
- `README.md` — feature bullets: retry now says "full jitter";
  new bullets for the hardened HTTP client and W3C-exact
  propagation; slab bullet notes any-slot-size safety.
- `docs/architecture.md` — the `protobuf_encode.c` module entry
  documents the 192 B SBO and why it is sized that way.

No code changes; build + full suite (38/38) ran as the gate.

## Verification

```
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout          # 38/38
```
