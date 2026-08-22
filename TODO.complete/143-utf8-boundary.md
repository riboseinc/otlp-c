# TODO 143 — UTF-8 boundary validation + composite golden vectors

**Status:** Complete (v0.5.103)
**Priority:** P1 (whole-batch rejection at collectors)

## The gap

OTLP string fields are proto3 `string`, which MUST be valid
UTF-8 — and the Go protobuf runtime used by otelcol REJECTS the
whole ExportRequest during unmarshal when any string field is
invalid. A single attribute value with, say, Latin-1 bytes from a
filename would silently kill the entire batch at the collector:
the exporter sees a 4xx/400, retries, drops after max retries —
and the user's telemetry vanishes with a confusing HTTP error.
The library validated nothing (`OTLP_ERR_UTF8` has existed in
the status enum since v0.1, unused).

## What shipped

**Validator** (`internal_util`: `otlp_str_is_utf8`): standard
multi-byte walk — rejects stray continuations, truncated
sequences, overlong encodings, UTF-16 surrogates, >U+10FFFF, and
5-byte+ leads; the NUL terminator makes truncation unpassable.

**Boundary wiring** (validate at ingestion; trust internal state
after — O(n) once per set, never per encode):

- the set-attribute ENGINE covers all six attribute surfaces at
  once: keys at the single reserve chokepoint, string values in
  `otlp_attr_vec_set`, and composite items/entry keys/values in
  `value_fill` (shared by ARRAY and KVLIST builders — which now
  propagate the real status instead of flattening failures to
  NOMEM)
- scalar wire strings: span name/trace_state/status description/
  event name, metric name/unit/description, log body/
  severity_text, exporter service_name; resource attributes flow
  through the engine at create
- `bytes` values are exempt (proto3 `bytes` accepts anything) —
  pinned by test

Invalid input fails the SETTER with `OTLP_ERR_UTF8`; creates
return NULL. The enum's strerror entry lights up for the first
time.

**Composite golden vectors**: the corpus now includes an ARRAY
attribute (string/int/bool — the false bool also pins oneof
presence semantics) and a KVLIST attribute (string/double) —
the composite encoders are reference-validated for the first
time; byte-length identical (420 = 420).

**Docstring corrections**: `get_stats()` reclassified as
thread-safe (it was already atomic-loads-only — the header
overstated the restriction); exporter.h documents the UTF-8
contract; status.h explains when ERR_UTF8 fires.

## Verification

- New `unit-utf8` (5 tests): validator vectors (valid: 2/3/4-byte
  sequences, emoji, DEL; invalid: lone continuation, truncations,
  overlongs, surrogates, 5-byte lead, 0xfe/0xff), per-surface
  rejection (span/metric/log/exporter + composites), bytes-exempt.
- Full matrix green (46 tests), fresh Release tree zero warnings.
- Golden vectors match with composites included.
