# TODO 179 — architecture.md docs truth (post-1.1.3 sweep)

**Status:** Complete (v1.1.4)
**Priority:** P3 (documentation)

## The drift

architecture.md lagged the code by three releases:

- The layer diagram's exporter box said "Metrics/Logs: sync
  flush" — false since v0.5.28 (all three signals async). It
  also omitted exporter_sync.c, extracted in v1.1.2.
- The MECE module table had no exporter_sync.c row.
- Testing strategy claimed "27 property tests" (25) and a
  unit-test inventory from the v0.5 era.
- CLAUDE.md key-files lacked tests/test_portable.h (v1.1.3).

## The fix

Box rewritten (byte-aligned with its neighbors), table row
added, diagnostics note extended to cover SYNC_FLUSH_FAILED,
testing section restated from live ctest counts, CLAUDE.md row
added. Docs-only release; no code, no wire surface, no API.
