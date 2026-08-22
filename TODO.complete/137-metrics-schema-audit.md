# TODO 137 — Metrics wire-schema audit against opentelemetry-proto

**Status:** Complete (v0.5.97)
**Priority:** P1 (live data-corruption bug)

## The gap

`src/otlp_schema.h` is the single source of truth for every wire
field number — and nothing checked it against the actual
opentelemetry-proto definitions. An audit of the metrics tables
against upstream `metrics.proto` (fetched live, not from memory)
found:

1. **LIVE: HistogramDataPoint min at field 10, max at 11.**
   Upstream is `flags = 10` (uint32 varint), `min = 11`,
   `max = 12`. Wire impact: collectors dropped our FIXED64 `min`
   as an unknown field, and decoded our `max` at 11 as *their*
   `min` — every exported histogram lost its minimum and
   reported a doubled max. Live since the metrics signal
   shipped.
2. **Dormant: EHDP `flags` declared fixed32.** Upstream is
   `uint32 flags = 10` (varint). The field is not emitted today,
   so no wire impact — but the entry would have produced a
   corrupt field the day someone starts emitting flags.
3. **Comment: LogRecord field 4 labeled observed_time.** Field 4
   is reserved upstream; `observed_time_unix_nano` is 11.

Root cause of (1): the HDP table was built half from the
pre-attributes-relocation proto (min/max kept the old 10/11) and
half post-relocation (attributes moved to the new 9) — upstream
PR #465 moved attributes onto the data points and shifted
everything after the reserved/exemplar range.

**Why no test ever caught it:** every existing test walked the
wire using the schema's own field numbers. A self-referential
check can only ever agree with the schema — bug included. The
integration test exercises spans only; no real collector ever
received an otlp-c histogram; docs/otlp-spec.md never documented
metrics data-point field numbers.

## What shipped

**Schema fixes** (`src/otlp_schema.h`): HDP `min` 10→11, `max`
11→12; comment now notes fields 8 (exemplars) and 10 (flags) as
not-emitted. EHDP `flags` wire type FIXED32→VARINT. LogRecord
comment corrected (field 4 reserved; observed_time = 11).

**Known-answer wire test**
(`tests/unit/test_unit_wire_numbers.c`, `unit-wire-numbers`):
all field numbers/wire types in the file are literals copied
from opentelemetry-proto — never derived from `otlp_schema.h`.
Three tests: (1) encode a recorded histogram, descend the
envelope, assert min lands at 11 and max at 12 (plus count@4,
sum@5, packed-fixed64 buckets@6, packed-double bounds@7) and
that **no field 10 ever appears** — the direct tripwire for
this bug recurring; (2) exp-histogram wire walk (scale zigzag@6,
buckets@8, offset zigzag@1, varint-packed counts@2); (3) pin
every NDP/HDP/EHDP/EHB/Metric/LogRecord table entry against
upstream literals, catching dormant entries (e.g. EHDP flags)
that wire tests can't reach because they're never emitted.
Also pins LogRecord flags as fixed32 — genuinely fixed32
upstream — so nobody "harmonizes" it with EHDP's varint flags.

**Test discipline:** decoder calls advance the reader, so none
appear inside `assert()` (Release/NDEBUG would elide them and
desynchronize the walk) — every call is a statement feeding a
flag; asserts inspect flags only.

## Verification

- New test FAILS against the pre-fix schema (min found at 10,
  field-10 tripwire fires) — confirmed by construction of the
  assertions.
- Debug 43/43, Release 43/43, ASAN 43/43 (leak check on),
  UBSAN 43/43. Zero new warnings.

## Remaining work (follow-up candidates)

- `docs/otlp-spec.md` documents trace field numbers but not
  metrics/logs data-point tables — the doc gap that let this
  drift hide. Worth a docs release extending the reference.
- CI could run a cross-validation against `protoc --decode_raw`
  or pinned golden vectors generated from opentelemetry-proto
  (the Phase 2 plan's golden-.bin idea, never built).
- Release-only `-Wunused-but-set-variable` warnings in older
  tests (`st` consumed only by elided asserts) — benign, but a
  zero-warnings-in-Release pass would restore the invariant.
