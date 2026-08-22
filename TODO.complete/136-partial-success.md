# TODO 136 — Surface OTLP PartialSuccess from collector responses

**Status:** Complete (v0.5.96)
**Priority:** P2 (spec-conformance gap)

## The gap

The OTLP spec's mechanism for server-side data loss reporting is
Export*PartialSuccess: a collector answers **200 OK** while
declaring, in the protobuf response body, how many items it
rejected (queue full, size limits, ...) and why. The exporter
discarded 200-response bodies entirely — a collector silently
dropping half the telemetry looked identical to a fully
successful export. The spec explicitly requires clients to
surface this (opentelemetry-proto: "the client SHOULD surface
this message").

## What shipped

**New module `src/protobuf_decode.{h,c}`** — the decode
counterpart of protobuf_encode (the library's first wire-format
reader). Bounds-checked primitives: `otlp_pb_read_key` (rejects
field number 0), `otlp_pb_read_varint` (rejects >10-byte varints),
`otlp_pb_read_len` (rejects lengths past end-of-buffer),
`otlp_pb_skip` (all wire types; rejects groups 3/4). Malformed
input returns false — never reads out of bounds.

**Schema** (`src/otlp_schema.h`): two decode-only field-spec
tables (Export*ServiceResponse partial_success=5; PartialSuccess
rejected=1, error_message=2). The three per-signal message pairs
upstream are field-identical — one shared table each (DRY);
single source of truth stays intact.

**Domain decode** (`exporter_otel.{h,c}`):
`otlp_exporter_otel_decode_partial_success()` — walks the
response body via the schema tables, skips unknown fields
(forward compat), last-wins on duplicate partial_success (proto3
merge), error_message points into the body (no copy).

**Exporter** (`exporter.c`):
- `record_outcome` now takes an `http_outcome` bundle (status,
  Retry-After, body) — read from the request before `_free`.
- 2xx branch surfaces PartialSuccess: WARN diagnostic
  ("collector partial success: 2 of 3 spans rejected: queue
  full") + new per-signal stats `rejected_spans` /
  `rejected_metrics` / `rejected_logs` (table-driven via a new
  `rejected_counter` in the signal_record_path descriptor). The
  batch is NOT retried — a 200 is final and the rejected items
  are gone server-side.
- Sync one-shot flush path surfaces the WARN too (no stats — the
  message is the observability surface there).

**Incidental test-debt fixes:**
- `prop_resource_full_value_model` (the v0.5.92 BYTES property)
  was defined but never registered in main() — the v0.5.92 file
  rewrite dropped the registration and the compiler only warned.
  Registered; running again.
- Unused variable in test_allocator_oom.c (zero-warnings
  invariant restored).
- Four more side-effecting asserts in the new wire test caught
  by the Release run BEFORE merge this time (echo_server_start,
  flush inside assert) — the v0.5.82 rule's sixth sighting,
  this time prevented pre-merge by the local Release gate.

## Tests

- unit-protobuf: 11 → 17 tests (key/varint/len roundtrips via
  the encoder, malformed vectors — truncated varint/length,
  field 0, groups — skip across all wire types, and six
  PartialSuccess scenarios built with the encoder itself).
- New fuzz prop `prop_fuzz_partial_success` (5000 iters): pure
  noise + mutated valid bodies through the decoder; no crash,
  and any returned message pointer must lie inside the input
  buffer.
- New wire test `exporter-partial-success` (3 cases): spans
  (rejected=2 of 3 + server message surfaced in WARN), logs,
  clean 200 (no rejection counted, no WARN).
- Debug + Release 42/42; ASAN clean (only the documented macOS
  Network-framework leak in property-url-parse); UBSAN 100%.
  Zero build warnings.

## Remaining work

None for this TODO. Future: gRPC-status decoding from
`application/grpc` error bodies is out of scope (no gRPC
transport); OTLP/JSON responses are out of scope (no JSON
transport).
