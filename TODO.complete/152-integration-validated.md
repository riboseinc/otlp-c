# TODO 152 — Integration suite re-validated against a live stack

**Status:** Complete (v0.6.6)
**Priority:** P2 (the end-to-end path had not run since v0.5.94)

## What ran

Docker (otelcol-contrib 0.159.0 + Jaeger all-in-one) brought up
COLD; `OTLP_C_RUN_INTEGRATION=1 ctest -L integration`:

- 100 spans (2 batches, async emit + tick), one-shot metric and
  log via the sync flush paths
- span + event + status verified by querying the span back out
  of Jaeger's API with the unique test_run_id

## Finding (fixed)

The 10s Jaeger-visibility budget is too tight for a cold
pipeline: otelcol's jaeger exporter flushes on a 5s batch timer
before Jaeger indexes anything. First cold run FAILED at 10s;
warm rerun passed. Budget raised to 30s; the validating cold run
passed at 24.8s — past the old budget, inside the new one.

## Evidence value

The run is the first real-collector exercise of everything
since v0.5.94: Retry-After-aware HTTP handling, the
PartialSuccess response decoder, the UTF-8 boundary, and the
v0.5.100 event model — the "batch sent: 50 spans" lines in the
test output ARE the event formatter's derived messages flowing
from a live otelcol interaction.

## Remaining

The compose file pins `:latest` for the collector image —
reproducibility would favor a pinned tag, at the cost of manual
refreshes. Left as-is deliberately (the suite is manual-run;
`latest` keeps it probing current collector behavior).
