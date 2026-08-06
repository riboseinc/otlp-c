# TODO 21 — Add OTLP logs signal (POST /v1/logs)

**Status:** Complete
**Priority:** P1
**Depends on:** nothing

## Goal

Implement LogRecord type with structured body, attributes, severity, and trace correlation.

## What shipped (v0.4)

**Public API** (`include/otlp-c/log.h`):
- `otlp_log_record_t` opaque type.
- `otlp_severity_t` enum (24 levels: TRACE…FATAL4, matching opentelemetry-proto).
- `otlp_log_record_create(severity, body)` / `_free()` lifecycle.
- `set_timestamp` / `mark_timestamp`.
- `set_trace_id` / `set_span_id` (16 / 8 byte buffers).
- `set_severity_text`.
- Attribute setters: string / int (cap 128).

**Internal layout** (`src/log_internal.h`, `src/log.c`):
- severity enum, severity_text, body string, timestamp, has_timestamp.
- trace_id[16], span_id[8], has_trace.
- 128-attribute fixed cap (matches spans/metrics).

**Wire encoder** (`src/otlp_logs_encoder.c`):
- `otlp_encode_export_logs_service_request()` produces ExportLogsServiceRequest bytes.
- Envelope: ResourceLogs → Resource{service.name} + ScopeLogs → Scope + LogRecord[].
- Reuses shared helpers from `otlp_messages.c`: `otlp_emit_resource`,
  `otlp_emit_instrumentation_scope`, `otlp_encode_any_value`,
  `otlp_encode_key_value`.
- `severity_number` omitted when UNSPECIFIED (zero value).
- `body` emitted as AnyValue.string_value via the table-driven dispatch.
- `trace_id` / `span_id` emitted as length-delimited bytes (fields 9/10).

**Property tests** (`tests/property/test_property_logs.c`):
- `prop_logs_empty_request` — no logs → 0 bytes.
- `prop_logs_severity_present` — INFO emits field 2 varint.
- `prop_logs_severity_omitted` — UNSPECIFIED produces 0 bytes.
- `prop_logs_body_string_roundtrip` — body string round-trips (200 iters).
- `prop_logs_trace_correlation` — trace_id/span_id bytes preserved (50 iters).
- `prop_logs_attributes_roundtrip` — int attribute round-trips (200 iters).

## Acceptance criteria
- [x] CI green on all platforms (build verified locally on macOS arm64).
- [x] No regression in existing tests.
- [x] Property tests pass deterministically.

## Out of scope (deferred)
- Dynamic attribute arrays (current cap: 128).
- Non-string body types (int/double/bool/bytes/kvlist AnyValue variants).
- `dropped_attributes_count` field.
- `flags` field (W3C trace-flags).
