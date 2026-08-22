# TODO 142 — Hygiene catch-up: stress joins + event example

**Status:** Complete (v0.5.102)
**Priority:** P3 (leftovers from 138/140)

## What shipped

**concurrency-stress worker lifetime** (the 138 leftover): the
echo worker was started with `requests_to_serve = 100` for a
scenario that sends a variable count (~13), so the final
`echo_server_join` timed out silently with the worker still in
accept() — the exact unchecked-join hazard class from v0.5.96/98,
present since the stress test was written. Now: `echo_server_stop()`
(self-connect wake — the request count is not knowable a priori)
+ checked join, checked joins on both error paths, and the eight
worker-thread `pthread_join`s are checked too. **Never `(void)` a
join** now holds everywhere in the tree.

**Event-callback example** (the 140 leftover):
`examples/minimal.c` installs `otlp_exporter_set_event_logger()`
with a tallying callback (BATCH_SENT / ITEMS_DROPPED with
drop_reason/signal called out) and prints the tally at exit —
the canonical "diagnostics as data" pattern next to the existing
string-logger docs.

## Verification

- Full matrix green (45 tests), examples build and run, zero
  Release warnings.
