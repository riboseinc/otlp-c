# TODO 183 — fourth architecture review; ADR 0006 (v1.1.8)

**Status:** Complete (v1.1.8)
**Priority:** P2 (architecture)

## What was done

Review #4 of the architecture skill: explored the hot spots
(exporter.c 1471, otlp_messages.c 788, internal_util.c 880,
the encoder layering, the internal include graph), applied the
deletion test to every remaining extraction candidate. All four
fail it. The report (Tailwind, self-contained) presented the
candidates as declined with reasons; the deliverable is ADR
0006, which records the decline so review #5 doesn't re-litigate.

## The declined candidates and why

1. Diagnostics extraction — depth is the one-model/one-formatter
   invariant (v0.5.100), not the filename; extraction adds a
   seam to relocate ~180 lines.
2. otlp_messages.c split — the shared surface already IS the
   header; metrics/logs encoders consume exactly it.
3. Pure outcome classifier — the retry half is already pure
   (retry_policy.c); the remainder is glue.
4. internal_util.c split — declined R1–R3; the one-engine
   locality is the design.

## The positive evidence

Uniform encoder layering (messages+schema+protobuf_encode ×3),
cross-seam-free include graph, 82% floor cleared per file,
docs matching the code after four docs-truth releases. The next
module-shape change should be a 4th signal: one SIGNAL_SPECS[]
row + one encoder, by the table's design.
