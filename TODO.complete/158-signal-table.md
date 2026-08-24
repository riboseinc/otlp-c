# TODO 158 — One signal table, not five descriptor families

**Status:** Complete (v0.6.12)
**Priority:** P2 (locality for the add-a-signal change)

## The friction

Traces, metrics, and logs were dispatched through FIVE descriptor
families in exporter.c — signal_drain_path, signal_emit_path,
signal_path, signal_record_path, signal_start_path — each with
three instances hand-assembled at a different call site:
`create()`/`free()` (drains), all six `emit*` wrappers (stack
locals), `tick()` (paths[3]), `record_path_for()` (a switch), and
the three `try_start_*_post` wrappers. Adding a signal (OTLP
profiles exists upstream) meant editing six regions of a
2006-line file.

## What shipped

- **`struct signal_state sig[3]`** in the exporter: queue, pending
  batch (void**), batch timer, and the five per-signal counters —
  replacing fifteen hand-named fields per signal.
- **`SIGNAL_SPECS[N_SIGNALS]`**: one static const row per signal —
  public id, diagnostics name, free/clone/build-request adapters.
  `signal_name()` derives from it; there is no other switch on
  signal identity.
- Every generic driver now dispatches through the table: emit
  commons take a kind index; `tick()` loops `e->sig[s]` with no
  descriptor assembly; `record_outcome` indexes by
  `in_flight_signal`; ONE `try_start_post(e, s)` replaced the
  three wrappers; `create()`/`free()`/`get_stats()` loop.
- exporter.c: 2006 → 1738 lines (net −268; −662/+394).

## Verification

- 50/50 in Debug, Release (zero warnings), ASAN+leaks, UBSAN,
  TSAN — the stats-accounting suites (events, retry,
  partial-success, stress×3) pin per-signal counters exactly.
- **Mutation-tested**: swapping the METRIC row's public id to
  OTLP_SIGNAL_LOGS aborts exporter-events — the table's
  id mapping is pinned by tests, not just convention.
