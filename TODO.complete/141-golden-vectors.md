# TODO 141 — Golden vectors: reference-validated payloads

**Status:** Complete (v0.5.101)
**Priority:** P2 (encoding-semantics verification; carried from 137/139/140)

## The gap

The schema pins (v0.5.99) verify every field NUMBER against
opentelemetry-proto literals, and the two wire-walk tests verify a
handful of encodings end-to-end. Nothing validated whole PAYLOADS
against the reference implementation — zigzag on real values,
packed-repeated encodings, presence rules across every message,
AnyValue oneof selection, nested envelope structure. That class of
drift would only surface at a real collector.

## What shipped

**Generator** (`tests/golden/generate.py`, manual regeneration;
requires `pip install opentelemetry-proto`): builds three
Export*ServiceRequest payloads with the REFERENCE Python classes
(pinned opentelemetry-proto 1.44.0) — not our encoder — and
writes raw `.bin` files (inspectable with `protoc --decode_raw`)
plus a generated `golden_vectors.h` embedding the bytes as C
arrays (no runtime file dependency, portable to Windows).

**Fixtures** cover the tricky surface in one pass: all six
AnyValue variants including a negative int64 (10-byte varint) and
binary bytes; span with trace_state/status/event/link; histogram
with buckets+bounds+min/max (the v0.5.97 fields) and all-exact
binary arithmetic so sums match bit-for-bit; exp-histogram with
negative scale (zigzag), zero_count, explicit positive/negative
bucket arrays; counter (Sum + is_monotonic); two log records with
trace correlation. Fixture values are duplicated (by design) in
the generator and the C test — drift between them is a failure.

**Comparator** (`tests/unit/test_unit_golden.c`): parses both
payloads into canonical field trees using the library's own
bounds-checked reader (plus new `otlp_pb_read_fixed32/64`
primitives added to `protobuf_decode` — the decode counterpart of
the encoder's fixed-width writers), then compares SEMANTICALLY:
fields matched by (number, wire type), repeated order preserved,
LEN payloads compared recursively when both parse as messages,
raw otherwise. Valid field reordering never fails; any drift in
numbers, wire types, values, packing, or presence does, with a
path to the mismatch.

## Findings

1. Our span emits `flags = 1` (W3C sampled) by default while the
   reference fixture left flags unset — fixture alignment, not a
   library bug; the generator now sets flags=1 to mirror our
   default-sampled spans. After alignment all three vectors are
   byte-length-identical to the reference serialization.
2. **The comparator's first version was vacuous** (mutation
   testing caught it): both trees shared one node pool, so
   parsing "ours" overwrote the golden tree and the comparison
   ran ours-vs-itself — a fixture flip sailed through. Fixed with
   one pool per side. Lesson reinforced: ALWAYS mutation-test a
   new test before trusting it; enforcement is a property to
   prove, not assume.

## Verification

- Mutation tests: flipping one fixture string fails exactly the
  affected vector with a path; metrics/logs still pass.
- All vectors match in every configuration; full suite green
  (45 tests).

## Remaining work

- Extending the corpus (e.g. ARRAY/KVLIST attributes, more log
  shapes) is now a fixture edit + regeneration away — do it when
  a change needs the coverage, not speculatively.
- Hygiene leftovers from 138/140: event-callback example;
  concurrency-stress join checks.
