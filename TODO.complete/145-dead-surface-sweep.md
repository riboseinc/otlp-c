# TODO 145 — Dead-surface sweep: poll_fds coverage, event-loop example, strerror pin

**Status:** Complete (v0.5.105)
**Priority:** P2 (exported API with zero coverage)

## The gaps

1. **`otlp_exporter_poll_fds()` had ZERO coverage** — exported
   since v0.1, documented as THE event-loop integration surface,
   never tested or exemplified. v0.5.104 audited the surface's
   DOCS; this sweep audited its COVERAGE.
2. **The Phase 7 event-loop example was never built** — the
   original plan's `examples/event_loop_integration.c` deliverable
   (demonstrate the poll-fd embedding pattern) had quietly never
   happened.
3. **`otlp_strerror()` completeness was unpinned** — 20/20 today,
   but a new status code without a message entry would silently
   fall to the default branch.

## What shipped

**`tests/test_exporter_poll.c`** (2 tests, echo server): the
zero-fd + argument-validation contract, and the integration that
matters — a REAL `poll()` loop driven off the exposed fd +
interest bits until the POST completes (sent == 1, http_2xx == 1,
checked worker join).

**Contract wart the test caught** (fixed): `out=NULL` with
`cap>0` returned `OTLP_OK` when no request was in flight, because
the state check preceded argument validation. Now validated
first — `OTLP_ERR_NULL` regardless of exporter state.

**`examples/event_loop_integration.c`** (POSIX, `<poll.h>`): the
embedding pattern end-to-end — poll on the exposed fds while a
request is in flight, poll-as-sleep for the batch timer
otherwise, event-logger tally narrating sent/dropped/retries.
With no collector it exits cleanly after the retry budget; with
one, the span ships.

**`tests/unit/test_unit_common.c`**: strerror completeness pin —
every status code maps to a non-empty, pairwise-distinct message
(distinctness matters: a duplicated message hides a mis-mapped
case as effectively as a missing one).

## Verification

Full suite green (48 tests: 46 + exporter-poll + unit-common) in
Debug; the example runs standalone. (Full matrix results in the
release notes.)
