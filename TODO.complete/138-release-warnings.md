# TODO 138 — Zero-Release-warnings suite + always-evaluated checks

**Status:** Complete (v0.5.98)
**Priority:** P1 (Release CI verified almost nothing)

## The gap

Two faces of the same NDEBUG disease, both already in the
project's paid-for-lessons memory but only half-fixed:

1. **The known face** (v0.5.82–95): side-effecting calls inside
   `assert()` vanish under Release — pthread_create, bind,
   parse_url, emit_move all disappeared at various points. Each
   fix converted the offending site to statement + pure assert.
2. **The quieter face** (fixed wholesale here): plain
   value-asserts — `assert(st == OTLP_OK)`,
   `assert(stats.sent == 10)` — are ALSO elided under Release.
   The side effect runs, but the CHECK doesn't. A fresh
   Release build of the test tree showed ~100
   `-Wunused-but-set-variable` / `-Wunused-parameter` /
   `-Wunused-variable` warnings: the compiler telling us exactly
   which assertions cease to exist in the configuration CI's
   plain jobs run. 536 assertion sites across 12 files.

A Release run of the unit tests therefore verified almost
nothing: no outcome could fail the test short of a crash.

## What shipped

**`tests/test_util.h`** — `check_ok(otlp_status_t)` and
`check_true(int)`: the expression is an ARGUMENT, so it is
evaluated in every configuration; `assert()` inside the helper
preserves Debug diagnostics; the `(void)` inside silences
Release. Statement-position, no macros, no control flow
(project style rules hold).

**Sweep**: all 536 statement-position `assert(` sites in the 12
warning-bearing test files converted (`tests/test_*.c` × 6,
`tests/unit/test_unit_*.c` × 6, including the brand-new
v0.5.97 wire-numbers test — its own asserts had the same face).
Property tests untouched except the fuzz raw handler's
`resp_cap` guard (cleanup-path `(void)` joins in fuzz stay
deliberate: a fuzzed transport may legitimately never complete
the request, and a checked join there would flake).

**Bug the conversion exposed immediately**: exporter-echo's
`check_ok(echo_server_join(...))` aborted — the replaced
`(void)`-ed join had been returning OTLP_ERR_TIMEOUT silently
since the metrics/logs cases were added. The worker was started
with `requests_to_serve = 16` for a ~4-request scenario: it
stayed blocked in accept() and was still running when main
returned — the same stack-use-after-return hazard class fixed
for partial-success in v0.5.96. Fixed with `echo_server_stop()`
+ checked join. This is the third confirmed instance of the
worker-lifetime lesson; the memory file now generalizes it:
**never `(void)` a join**.

**Second latent bug the fix exposed** (CI TSAN failed the first
run): `echo_server_stop()` and the worker's exit path both
closed/wrote the plain-int `sock_fd` — a data race and potential
double-close present since the helper was written, never
triggered because all prior callers stopped already-exited
workers. Helper rewritten: `sock_fd` atomic, worker is the
single closer, `stop()` sets an atomic `stopping` flag and wakes
accept() via self-connect — the only portable wake (neither
shutdown() nor close() on a listening socket reliably unblocks
accept() on macOS; the old close-based mechanism had never been
exercised on a live worker). Full local TSAN suite: 43/43.

**Docs catch-up**: CLAUDE.md version line 0.5.86 → 0.5.97,
capability bullets for v0.5.95–97 (Retry-After was present;
PartialSuccess and the wire-schema audit now too), a new
test-writing rule mandating the helpers, and the roadmap
key-metrics line refreshed (138 TODOs, 43 tests, 45+ bugs).

## Verification

- Debug 43/43, Release 43/43 + **zero warnings**, ASAN 43/43,
  UBSAN 43/43 (macOS leak check on the touched exporter-echo).
- The Release run now executes all 536 previously-elided
  assertions — all passed, confirming no silent Release-only
  assertion failure existed beneath the warnings.

## Remaining work

- The property tests still use bare `assert()` in places where
  results are out-params consumed later (safe: the calls always
  run) — converting their value-checks would harden Release
  property runs the same way. Lower value (CI runs sanitizers
  on those files), deferred.
- `test_concurrency_stress.c` ignores three join returns against
  a file-scope server (no frame-death hazard; CI green).
  Convert opportunistically next time that file is touched.
