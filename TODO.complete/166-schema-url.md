# TODO 166 — schema_url emitted (conformance gap)

**Status:** Complete (v0.7.4)
**Priority:** P1 (verified wire gap)

## The gap

opentelemetry-proto carries schema_url as field 3 on
ResourceSpans / ResourceMetrics / ResourceLogs. Our schema tables
have it (and unit-wire-numbers pins it against the upstream
literal) — but no encoder ever emitted it: callers could not
state their telemetry schema at all.

## What shipped

- Public `otlp_exporter_opts_t.schema_url` (additive; UTF-8
  validated at create; deep-copied; NULL/empty = omitted).
- Threaded through the SIGNAL_SPECS build chain and the sync
  encode paths; emitted as field 3 on every signal's
  resource-level message.
- Wire-pinned in unit-wire-numbers: descend to the resource-level
  message for each signal, assert field 3 carries the exact
  value, and assert ABSENCE when the opt is NULL.

## Note

52 test/bench call sites of the encode entry functions gained
the parameter; the first mechanical fixer pass inserted it at the
wrong index (service names slid into the schema slot — caught by
the golden vectors and the resource-attrs property), reverted and
re-applied correctly.

## Verification

52/52 via default/release/asan/ubsan/tsan; fresh-configure build
zero warnings.
