# TODO 169 — Golden corpus extended; exemplar wire bug found+fixed

**Status:** Complete (v1.0.1)
**Priority:** P0 (released wire bug) + P1 (conformance coverage)

## The bug

Extending the golden vectors (reference opentelemetry-proto
serialization) to cover exemplars byte-compared against a
v0.8.0-built library EXPOSED that the v0.8.0 Exemplar schema
table was wrong: five of six field numbers hand-copied from
memory. Real proto (from the installed descriptor):
time_unix_nano=2, as_double=3, span_id=4, trace_id=5, as_int=6,
filtered_attributes=7. A real collector would misparse every
exemplar we emitted.

The wire-numbers pin DIDN'T catch it because the pin literals
were copied from the same wrong memory. Lesson recorded in the
pin comment: literals come from the installed descriptor, and
the GOLDEN vectors (reference implementation, not our tables)
are the authoritative gate — they caught it immediately once
exemplars were in the corpus.

## What shipped

- Schema table corrected; emitter renumbered; pin literals
  corrected with a provenance comment.
- Golden corpus: schema_url on all three signals, double+trace
  exemplar on the counter, int exemplar on the gauge —
  byte-matches the reference for all three vectors.
- Additive `otlp_exemplar_set_timestamp()` (span/log setter
  parity; the fixture uses it — no internal poking).
- `GOLDEN_DUMP=1` debug hook in unit-golden (writes our payloads
  for protoc decode — how this bug was diagnosed in minutes).
- deployment.md: the fictional OTLP_C_ENDPOINT replaced with the
  real OTEL_EXPORTER_OTLP_ENDPOINT; cookbook gains the v0.7/0.8
  feature sections.

## Verification

52/52 via every preset; Doxygen zero warnings; all three golden
vectors byte-match.
