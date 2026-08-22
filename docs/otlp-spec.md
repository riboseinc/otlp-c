# OTLP/HTTP Protocol Reference

This document is the canonical protocol reference for `otlp-c`. It
covers only what the library needs to implement: trace export via
OTLP/HTTP. Metrics and logs signals are tracked in
[roadmap.md](roadmap.md) as P1 features and will get their own
sections when added.

The authoritative source for the OTLP specification is the
[opentelemetry-specification](https://github.com/open-telemetry/opentelemetry-specation)
repo. The message schema lives in
[opentelemetry-proto](https://github.com/open-telemetry/opentelemetry-proto),
which is Apache 2.0 licensed.

## Wire format overview

OTLP/HTTP is **Protobuf-encoded message bodies sent via HTTP/1.1
POST requests**. Two body encodings are supported by collectors:

| Encoding | Content-Type | Notes |
|---|---|---|
| Protobuf | `application/x-protobuf` | **Default. Used by `otlp-c`.** Most compact. |
| JSON | `application/json` | Larger; useful for debugging. We do not emit JSON. |

`otlp-c` emits Protobuf. JSON encoding is tracked as P2.

## Endpoints

The OTLP/HTTP convention (per the spec) is paths under `/v1/`:

| Signal | Method | Path | Request type | Response type |
|---|---|---|---|---|
| Traces | POST | `/v1/traces` | `ExportTraceServiceRequest` | `ExportTraceServiceResponse` |
| Metrics | POST | `/v1/metrics` | `ExportMetricsServiceRequest` | `ExportMetricsServiceResponse` |
| Logs | POST | `/v1/logs` | `ExportLogsServiceRequest` | `ExportLogsServiceResponse` |

Default port: `4318` (HTTP) or `4317` (gRPC — not implemented).

A collector at `http://localhost:4318` is the standard local-dev
configuration. For remote collectors, run an `otelcol` on the same
host and let it handle TLS to the backend.

## Request format

```
POST /v1/traces HTTP/1.1
Host: <endpoint>
Content-Type: application/x-protobuf
Content-Length: <N>

<protobuf body>
```

Optional headers (sender-supplied):

| Header | Purpose |
|---|---|
| `User-Agent` | Conventionally includes SDK name + version. otlp-c sends `otlp-c/<version>`. |
| `X-Forwarded-For` | Standard. otlp-c does not set this. |

No authentication is performed at the HTTP layer by default;
collectors handle auth via their own config.

## Response format

```
HTTP/1.1 200 OK
Content-Type: application/x-protobuf
Content-Length: <N>

<protobuf body>
```

The response body is an `ExportTraceServiceResponse` (often empty
for a successful export).

### Status codes

| Code | Meaning | Retry? |
|---|---|---|
| 200 | Success | no |
| 400 | Bad request — malformed protobuf or invalid message shape | no |
| 401 | Unauthorized (collector rejected auth) | no |
| 403 | Forbidden | no |
| 404 | Wrong endpoint | no |
| 429 | Throttled — retry with backoff | yes |
| 500 | Internal collector error | yes |
| 502, 503, 504 | Upstream failure — retry with backoff | yes |

`otlp-c`'s retry policy: exponential backoff with full jitter,
capped at 30 seconds, up to 5 retries (default; configurable).

## Message schema

All message types are defined in
[opentelemetry-proto](https://github.com/open-telemetry/opentelemetry-proto/blob/main/opentelemetry/proto).
We hand-roll the C structs in `src/otlp_messages.h`. The field
numbers below are from the upstream `.proto` definitions and
must match exactly.

### ExportTraceServiceRequest

The top-level request body for `/v1/traces`.

```proto
message ExportTraceServiceRequest {
  repeated ResourceSpans resource_spans = 1;
}
```

Field | Type | Number
---|---|---
`resource_spans` | repeated `ResourceSpans` | 1

### ResourceSpans

Groups spans by their resource (the process emitting them).

```proto
message ResourceSpans {
  Resource resource = 1;
  repeated ScopeSpans scope_spans = 2;
  string schema_url = 3;
}
```

Field | Type | Number
---|---|---
`resource` | `Resource` | 1
`scope_spans` | repeated `ScopeSpans` | 2
`schema_url` | string | 3

### Resource

The entity emitting the telemetry. Conventionally includes
`service.name`, `host.name`, `process.pid`, etc.

```proto
message Resource {
  repeated KeyValue attributes = 1;
  uint32 dropped_attributes_count = 2;
}
```

Field | Type | Number
---|---|---
`attributes` | repeated `KeyValue` | 1
`dropped_attributes_count` | uint32 | 2

### ScopeSpans

Groups spans by their instrumentation scope (the library emitting
them, e.g. `otlp-c/0.1.0`).

```proto
message ScopeSpans {
  InstrumentationScope scope = 1;
  repeated Span spans = 2;
  string schema_url = 3;
}
```

Field | Type | Number
---|---|---
`scope` | `InstrumentationScope` | 1
`spans` | repeated `Span` | 2
`schema_url` | string | 3

### InstrumentationScope

The library emitting the spans.

```proto
message InstrumentationScope {
  string name = 1;
  string version = 2;
  repeated KeyValue attributes = 3;
  uint32 dropped_attributes_count = 4;
}
```

Field | Type | Number
---|---|---
`name` | string | 1
`version` | string | 2
`attributes` | repeated `KeyValue` | 3
`dropped_attributes_count` | uint32 | 4

### Span

A single trace span. This is the unit of telemetry.

```proto
message Span {
  bytes trace_id = 1;
  bytes span_id = 2;
  string trace_state = 3;
  bytes parent_span_id = 4;
  string name = 5;
  SpanKind kind = 6;
  fixed64 start_time_unix_nano = 7;
  fixed64 end_time_unix_nano = 8;
  repeated KeyValue attributes = 9;
  uint32 dropped_attributes_count = 10;
  repeated Event events = 11;
  uint32 dropped_events_count = 12;
  repeated Link links = 13;
  uint32 dropped_links_count = 14;
  Status status = 15;
  fixed32 flags = 16;
}
```

Field | Type | Number | Notes |
|---|---|---|---|
| `trace_id` | bytes (16-byte) | 1 | Random, per trace. |
| `span_id` | bytes (8-byte) | 2 | Random, per span. |
| `trace_state` | string | 3 | W3C Trace Context state. Default empty. |
| `parent_span_id` | bytes (8-byte) | 4 | Empty for root spans. |
| `name` | string | 5 | Human-readable. |
| `kind` | enum `SpanKind` | 6 | See below. |
| `start_time_unix_nano` | fixed64 | 7 | UTC nanoseconds since epoch. |
| `end_time_unix_nano` | fixed64 | 8 | UTC nanoseconds since epoch. |
| `attributes` | repeated `KeyValue` | 9 | Caller-supplied. |
| `dropped_attributes_count` | uint32 | 10 | When truncated. |
| `events` | repeated `Event` | 11 | Time-stamped annotations. |
| `dropped_events_count` | uint32 | 12 | |
| `links` | repeated `Link` | 13 | Cross-trace references. |
| `dropped_links_count` | uint32 | 14 | |
| `status` | `Status` | 15 | OK / ERROR / UNSET. |
| `flags` | fixed32 | 16 | Bit field. Bit 1 = W3C Trace Flags. |

#### SpanKind enum

| Value | Name | Meaning |
|---|---|---|
| 0 | UNSPECIFIED | (default) Implicitly a internal span. |
| 1 | INTERNAL | Internal to the application. |
| 2 | SERVER | Server-side RPC. |
| 3 | CLIENT | Client-side RPC. |
| 4 | PRODUCER | Message producer. |
| 5 | CONSUMER | Message consumer. |

### Status

Span outcome.

```proto
message Status {
  reserved 1;
  string message = 2;
  StatusCode code = 3;
}
```

Field | Type | Number
---|---|---
`code` | enum `StatusCode` | 3
`message` | string | 2

#### StatusCode enum

| Value | Name | Meaning |
|---|---|---|
| 0 | UNSET | (default) Decision deferred. |
| 1 | OK | Operation succeeded. |
| 2 | ERROR | Operation failed. |

### KeyValue

Generic key/value attribute. The value is a oneof over the
supported types.

```proto
message KeyValue {
  string key = 1;
  AnyValue value = 2;
}

message AnyValue {
  oneof value {
    string string_value = 1;
    bool bool_value = 2;
    int64 int_value = 3;
    double double_value = 4;
    ArrayValue array_value = 5;
    KeyValueList kvlist_value = 6;
    bytes bytes_value = 7;
  }
}
```

`otlp-c` supports the full oneof: `string`, `bool`, `int64`,
`double`, `bytes`, and the composite `ArrayValue` /
`KeyValueList` (via the `*_set_attribute_array` /
`*_set_attribute_kvlist` setters, which take flat arrays of the
public `otlp_value_t` scalar type — see `include/otlp-c/value.h`;
nesting composites inside composites is not expressible through
the public API). Attribute keys are unique per object: setting an
existing key replaces its value (last write wins, v0.5.73).

### Event

A time-stamped annotation inside a span.

```proto
message Event {
  fixed64 time_unix_nano = 1;
  string name = 2;
  repeated KeyValue attributes = 3;
  uint32 dropped_attributes_count = 4;
}
```

### Link

A cross-trace reference. Used when a span has multiple parents
(e.g. fan-in).

```proto
message Link {
  bytes trace_id = 1;
  bytes span_id = 2;
  string trace_state = 3;
  repeated KeyValue attributes = 4;
  uint32 dropped_attributes_count = 5;
  fixed32 flags = 6;
}
```

## Metrics messages

The top-level request body for `/v1/metrics`. Field numbers in
this section were absent from this reference until v0.5.99 — the
gap that let the HistogramDataPoint min/max drift (v0.5.97) hide.
The canonical in-tree source is `src/otlp_schema.h`;
`tests/unit/test_unit_wire_numbers.c` pins every table below
against these numbers in every build configuration.

### ExportMetricsServiceRequest

```proto
message ExportMetricsServiceRequest {
  repeated ResourceMetrics resource_metrics = 1;
}
```

### ResourceMetrics / ScopeMetrics

Same envelope shape as traces:

```proto
message ResourceMetrics {
  Resource resource = 1;
  repeated ScopeMetrics scope_metrics = 2;
  string schema_url = 3;
}
message ScopeMetrics {
  InstrumentationScope scope = 1;
  repeated Metric metrics = 2;
  string schema_url = 3;
}
```

### Metric

Oneof over the data shape. Fields 4, 6, 8 are reserved upstream.

```proto
message Metric {
  string name = 1;
  string description = 2;
  string unit = 3;
  Gauge gauge = 5;
  Sum sum = 7;
  Histogram histogram = 9;
  ExponentialHistogram exponential_histogram = 10;
}
```

### Sum / Gauge / Histogram / ExponentialHistogram

```proto
message Sum {
  repeated NumberDataPoint data_points = 1;
  AggregationTemporality aggregation_temporality = 2;
  bool is_monotonic = 3;
}
message Gauge {
  repeated NumberDataPoint data_points = 1;
}
message Histogram {
  repeated HistogramDataPoint data_points = 1;
  AggregationTemporality aggregation_temporality = 2;
}
message ExponentialHistogram {
  repeated ExponentialHistogramDataPoint data_points = 1;
  AggregationTemporality aggregation_temporality = 2;
}
```

### NumberDataPoint

Field 1 is reserved upstream (the attributes field moved to 7 in
opentelemetry-proto PR #465 — tables mixing pre- and
post-relocation numbers are how wire bugs are born).

```proto
message NumberDataPoint {
  reserved 1;
  fixed64 start_time_unix_nano = 2;
  fixed64 time_unix_nano = 3;
  double as_double = 4;
  repeated Exemplar exemplars = 5;   // not emitted by otlp-c
  sfixed64 as_int = 6;              // not emitted by otlp-c
  repeated KeyValue attributes = 7;
  uint32 flags = 8;                 // not emitted by otlp-c
}
```

Field | Type | Number
---|---|---
`start_time_unix_nano` | fixed64 | 2
`time_unix_nano` | fixed64 | 3
`as_double` | double (oneof) | 4
`attributes` | repeated `KeyValue` | 7

### HistogramDataPoint

```proto
message HistogramDataPoint {
  reserved 1;
  fixed64 start_time_unix_nano = 2;
  fixed64 time_unix_nano = 3;
  fixed64 count = 4;
  double sum = 5;
  repeated fixed64 bucket_counts = 6;   // packed fixed64
  repeated double explicit_bounds = 7;  // packed double
  repeated Exemplar exemplars = 8;      // not emitted by otlp-c
  repeated KeyValue attributes = 9;
  uint32 flags = 10;                    // varint; not emitted
  optional double min = 11;
  optional double max = 12;
}
```

`min`=11 / `max`=12 is exactly the v0.5.97 fix — otlp-c emitted
them at 10/11 for its whole life before that (collectors dropped
`min` and decoded `max` as `min`).

### ExponentialHistogramDataPoint

```proto
message ExponentialHistogramDataPoint {
  repeated KeyValue attributes = 1;
  fixed64 start_time_unix_nano = 2;
  fixed64 time_unix_nano = 3;
  fixed64 count = 4;
  double sum = 5;
  sint32 scale = 6;        // zigzag varint
  fixed64 zero_count = 7;
  ExponentialHistogramBuckets positive = 8;
  ExponentialHistogramBuckets negative = 9;
  uint32 flags = 10;       // varint; not emitted
  repeated Exemplar exemplars = 11;  // not emitted
  double min = 12;         // not emitted
  double max = 13;         // not emitted
  double zero_threshold = 14;        // not emitted
}
```

Note `flags` here is **varint** — unlike Span/Link/LogRecord
`flags`, which are fixed32.

### ExponentialHistogramBuckets

```proto
message ExponentialHistogramBuckets {
  sint32 offset = 1;                    // zigzag varint
  repeated uint64 bucket_counts = 2;    // packed VARINT — unlike
                                        // HistogramDataPoint's
                                        // packed fixed64
}
```

## Logs messages

The top-level request body for `/v1/logs`.

### ExportLogsServiceRequest

```proto
message ExportLogsServiceRequest {
  repeated ResourceLogs resource_logs = 1;
}
```

### ResourceLogs / ScopeLogs

```proto
message ResourceLogs {
  Resource resource = 1;
  repeated ScopeLogs scope_logs = 2;
  string schema_url = 3;
}
message ScopeLogs {
  InstrumentationScope scope = 1;
  repeated LogRecord log_records = 2;
  string schema_url = 3;
}
```

### LogRecord

```proto
message LogRecord {
  fixed64 time_unix_nano = 1;
  enum SeverityNumber severity_number = 2;
  string severity_text = 3;
  reserved 4;
  AnyValue body = 5;
  repeated KeyValue attributes = 6;
  uint32 dropped_attributes_count = 7;
  fixed32 flags = 8;
  bytes trace_id = 9;
  bytes span_id = 10;
  fixed64 observed_time_unix_nano = 11;  // not emitted
  string event_name = 12;                // not emitted
}
```

Field 4 is reserved upstream (it held severity before the enum
moved to 2); `otlp-c` does not emit fields 11/12.

## Protobuf wire format

Protobuf encodes messages as a sequence of fields. Each field is
a key followed by a value. The key encodes the field number and
the wire type.

### Wire types

| Type | Meaning | Notes |
|---|---|---|
| 0 | Varint | int32, int64, uint32, uint64, sint32, sint64, bool, enum |
| 1 | 64-bit | fixed64, sfixed64, double |
| 2 | Length-delimited | string, bytes, embedded messages, packed repeated fields |
| 5 | 32-bit | fixed32, sfixed32, float |

### Key encoding

Key = `(field_number << 3) | wire_type`, encoded as a varint.

### Varint encoding

Variable-length integer, 7 bits per byte. The MSB is the
continuation bit.

```
0xxxxxxx                                    (one byte, value < 128)
1xxxxxxx 0xxxxxxx                           (two bytes)
1xxxxxxx 1xxxxxxx 0xxxxxxx                  (three bytes)
...
```

### Length-delimited encoding

For wire type 2: a varint length prefix, then the bytes.

### Examples

A Span with `name = "hello"` and `kind = INTERNAL (1)`:

```
field 5, wire 2 (length-delimited): key = 0x2a, len = 5, "hello"
field 6, wire 0 (varint):           key = 0x30, value = 0x01
```

Wire bytes:
```
2a 05 68 65 6c 6c 6f   30 01
```

(9 bytes total.)

## Batching rules

- **Batch size**: max spans per HTTP request. Default 512.
- **Batch timeout**: max wall-clock time before flush. Default 100ms.
- **Backpressure**: when the exporter can't keep up, drop incoming
  spans and count. Property test P-EXPORT-NEVER-CORRUPT covers this.

## Retry policy

| Status | Action |
|---|---|
| 200 | Success. Reset retry counter. |
| 429 | Exponential backoff. Initial 1s, max 30s, full jitter. Up to 5 retries. |
| 5xx | Same as 429. |
| 4xx (other) | Permanent failure. Drop the batch. Increment error counter. |
| Network error | Same as 5xx. |

## Implementation notes for otlp-c

- **Varint encoder** (`src/protobuf_encode.c`): takes a `uint64_t`
  and emits 1–10 bytes. The hot path; property-test heavily.
- **Length-delimited encoder**: wraps the varint encoder to emit
  `len + bytes`.
- **Span encoder**: walks the C `Span` struct and emits the wire
  bytes field by field. Skip empty fields (Protobuf convention).
- **HTTP client** (`src/http_client.c`): raw socket, HTTP/1.1,
  POST. No TLS. Connect per request (P1: connection pool).
- **Exporter** (`src/exporter_otel.c`): batches spans, calls the
  HTTP client, handles retry with backoff.

## See also

- [OTLP specification](https://opentelemetry.io/docs/specs/otlp/) — the canonical spec.
- [opentelemetry-proto](https://github.com/open-telemetry/opentelemetry-proto) — the .proto definitions.
- [Protobuf wire format](https://protobuf.dev/programming-guides/encoding/) — official encoding reference.
- [opentelemetry-collector](https://github.com/open-telemetry/opentelemetry-collector) — the reference collector (`otelcol`).
