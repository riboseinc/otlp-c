# Changelog

All notable changes to `otlp-c` are documented here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the project adheres to [Semantic Versioning](https://semver.org/).

## [0.5.33] - 2026-08-09

Fixes data-loss bug in span_clone + stats gap in sync flush.

### Fixed — span_clone dropped event and link attributes

`otlp_span_clone` copied events with only `name` + `time` and
links with only `trace_id` + `span_id`. Event and link
**attributes were silently dropped**. Every `emit()` call that
cloned a span with event/link attributes sent incomplete data
to the collector — the attributes existed on the original span
but were lost in the deep-copy.

Fix: clone now copies event attributes via `otlp_attribute_copy_all`
(the shared helper from v0.5.31/v0.5.32) into each event's
`attrs[]` array. Same for link attributes.

The bug existed since events/links were added to the span struct.
Not caught earlier because the existing `prop_span_clone_copies_extras`
test didn't add attributes to events/links (only tested name +
time + trace_id + span_id).

New regression test: `prop_span_clone_preserves_evlink_attrs` —
adds an event with a string attribute, adds a link with a string
attribute, clones, and verifies the clone preserves both.

### Fixed — flush_metric / flush_log don't update per-signal stats

The synchronous flush functions (`otlp_exporter_flush_metric`,
`otlp_exporter_flush_log`) didn't update the per-signal stats
counters (`emitted_metrics`, `sent_metrics`, `dropped_metrics_err`,
`emitted_logs`, `sent_logs`, `dropped_logs_err`). Only the async
pipeline (emit → tick → record_outcome) updated these.

A caller using `flush_metric` saw `sent_metrics=0` even after
successful sends — misleading stats.

Fix: both functions now increment `emitted_metrics` / `emitted_logs`
at entry, then `sent_*` on success or `dropped_*_err` on failure.

## [0.5.32] - 2026-08-09

Code quality cleanup: decoupled internal headers + span_clone DRY
refactor + cookbook patterns for 6 releases of features.

### Fixed — internal_util.h coupling

`internal_util.h` included `span_internal.h` (added in v0.5.31
for the `otlp_attribute_copy_all` declaration). This created a
dependency from the utility layer to the span layer — every file
including internal_util.h transitively pulled in span_internal.h.

Replaced with a forward declaration (`struct otlp_attribute;`).
The implementation file (`internal_util.c`) still includes the
full header; only the declaration header was decoupled.

### Changed — span_clone uses shared attribute-copy helper

`otlp_span_clone` was rebuilt using the public API
(`otlp_span_set_attribute_string` etc.) for each attribute — slow
(checks capacity, searches for existing keys per call). Replaced
with a single call to `otlp_attribute_copy_all` (the shared helper
from v0.5.31): direct struct manipulation, ~47 lines → 7 lines.

Also fixed `otlp_attribute_copy_all`: the `default: break` case
silently corrupted ARRAY/KVLIST attributes (set type, left union
value as zero/garbage). Now returns OTLP_ERR_NOMEM via `goto fail`
for unsupported types — same behavior as the old span_clone (which
returned error for ARRAY/KVLIST).

### Added — Cookbook patterns for v0.5.20–v0.5.28 features

6 new patterns in `docs/cookbook.md`:
- 11. Async metrics and logs (emit_metric_move + tick).
- 12. Production diagnostics (set_logger callback).
- 13. Resource attributes (typed values).
- 14. W3C Baggage propagation.
- 15. Metric temporality and is_monotonic.
- 16. Configurable flush timeout.

## [0.5.31] - 2026-08-09

emit_metric / emit_log (clone variants) — completes the emit API
symmetry across all three signals.

### Added — emit_metric + emit_log (deep-copy, caller keeps ownership)

v0.5.28 added `emit_metric_move` / `emit_log_move` (move
semantics — caller gives up ownership). v0.5.31 adds the clone
counterparts:

```c
otlp_status_t otlp_exporter_emit_metric(otlp_exporter_t *exp,
    const otlp_metric_t *metric);

otlp_status_t otlp_exporter_emit_log(otlp_exporter_t *exp,
    const otlp_log_record_t *log);
```

These deep-copy the metric/log before pushing into the MPSC
queue. The caller keeps ownership and may reuse or free the
original immediately. Slower than the move variant (one extra
alloc per attribute); use when the caller needs the original
after emit (e.g., emitting to multiple exporters).

This completes the API symmetry:

| Signal | Clone (keep) | Move (give up) |
|---|---|---|
| Span | `emit()` | `emit_move()` |
| Metric | `emit_metric()` | `emit_metric_move()` |
| Log | `emit_log()` | `emit_log_move()` |

### Added — Internal clone functions

- `otlp_metric_clone(src)` — deep-copies all metric fields
  (name, unit, description, timestamps, attributes, value,
  histogram bounds/counts, exp-histogram arrays, agg_temp,
  is_monotonic).
- `otlp_log_record_clone(src)` — deep-copies severity, body,
  timestamps, trace_id, span_id, attributes.

Both use the shared `otlp_attribute_copy_all()` helper extracted
into `internal_util.c` (DRY — same attribute-copy logic used by
both, available for future span_clone refactoring).

### Added — Property test

`prop_async_metric_emit_clone` in `test_property_async_metrics.c`:
emit_metric (clone) + tick + verify sent_metrics=1 AND the
original metric is still usable (caller kept ownership).

## [0.5.30] - 2026-08-09

tick() DRY refactor — eliminates signal triplication introduced
in v0.5.28 (async metric/log batching).

### Changed — tick() table-driven signal dispatch

v0.5.28 added metric/log support to tick() as three parallel
code blocks (drain, null-transport, POST start, backoff retry).
Each block was structurally identical, differing only in which
queue, pending array, timer, and start_post function to use.

Replaced with a `struct signal_path` descriptor table:

```c
struct signal_path {
    struct mpsc_queue *queue;
    void **pending;
    size_t pending_cap;
    size_t *pending_count;
    bool *first_set;
    uint64_t *first_mono;
    int signal_kind;
    otlp_status_t (*start_post)(struct otlp_exporter *e);
};
```

tick() builds `paths[3]` once (pointers into the exporter
struct), then iterates:
- Drain: one `for (s = 0; s < 3; s++)` loop replaces three
  identical while loops.
- Null-transport: one loop tries signals by priority.
- POST start: one loop checks batch-ready conditions.
- Backoff retry: `paths[in_flight_signal].start_post(e)` replaces
  the switch statement.

Net effect: ~80 lines of triplicated code reduced to ~30 lines of
looped code. Adding a fourth signal is one `paths[]` entry, not
another parallel block (OCP). All 33 tests pass unchanged — the
refactor is behavior-preserving.

## [0.5.29] - 2026-08-09

Documentation accuracy audit — catches CLAUDE.md, architecture.md,
and README.md up to v0.5.28 (13 releases of accumulated changes).

### Fixed — CLAUDE.md stale claims

CLAUDE.md (the file every contributor reads first) was last
updated in v0.5.16. Since then, 13 releases shipped:

- "All phases are complete (v0.5.15)" → updated to v0.5.28.
- Feature list: added async metric/log batching, W3C Baggage,
  diagnostic callback, typed Resource attributes, metric
  temporality/is_monotonic, HTTP timeout enforcement,
  configurable flush timeout, compile-time cap overrides.
- Key files table: updated exporter.c (now 3 MPSC queues),
  context.h (now includes Baggage), http_client.c (now includes
  timeouts). Added bench/bench_emit.c.
- Conventions: added sections on three-signal pipeline,
  diagnostics, and per-signal stats.

### Fixed — docs/architecture.md stale pipeline description

- "Metrics and logs are flushed synchronously" → updated to
  reflect v0.5.28's async pipeline for all three signals.
- Module table: exporter.c updated from "MPSC queue" to
  "3 MPSC queues (span/metric/log)".

### Fixed — README.md stale feature list

- Metrics: "Synchronous flush" → "Async emit + synchronous flush
  fallback."
- Logs: same update.
- Context: added W3C Baggage.
- Added Diagnostics, Resource attributes bullets.
- Status banner: 0.5.18 → 0.5.28.

This is the same kind of accuracy audit as v0.5.16 (CLAUDE.md)
and v0.5.19 (policy docs) — stale claims that mislead every
reader until fixed.

## [0.5.28] - 2026-08-09

Async metric/log batching — closes the #1 architectural gap.
Metrics and logs now flow through the same MPSC + tick + retry
pipeline as traces, instead of blocking the caller on HTTP.

### Added — Async metric/log emission

Two new public functions (move semantics — caller gives up
ownership, same contract as `otlp_exporter_emit_move` for spans):

```c
otlp_status_t otlp_exporter_emit_metric_move(otlp_exporter_t *exp,
    otlp_metric_t *metric);

otlp_status_t otlp_exporter_emit_log_move(otlp_exporter_t *exp,
    otlp_log_record_t *log);
```

These push into new MPSC queues (`metric_queue`, `log_queue`)
and return immediately. `tick()` drains all three signals (span,
metric, log) by priority, batches per signal, encodes, and POSTs
to the correct endpoint (`/v1/traces`, `/v1/metrics`, `/v1/logs`).

One in-flight HTTP request at a time (shared across all signals).
Retry/backoff is shared — a failure on any signal briefly backs
off all signals, preventing hammering a broken collector.

The existing `flush_metric()` / `flush_log()` synchronous
functions remain as fallbacks for low-frequency, one-shot export.

### Changed — tick() handles all three signals

`tick()` now:
1. Drains span, metric, and log queues into separate pending
   arrays.
2. Null-transport fast path tries span first, then metric, then
   log.
3. POST start checks all three signals' batch-ready conditions
   (same `batch_size` / `batch_ms` / `shutdown` logic).
4. Backoff retry dispatches based on `in_flight_signal` (which
   signal was last in-flight).

The span path is structurally unchanged — all existing span
tests pass without modification.

### Added — Per-signal stats

`otlp_exporter_stats_t` extended with 8 new fields:

```c
uint64_t emitted_metrics, sent_metrics;
uint64_t dropped_metrics_full, dropped_metrics_err;
uint64_t emitted_logs, sent_logs;
uint64_t dropped_logs_full, dropped_logs_err;
```

Existing span counters (`emitted`, `sent`, `dropped_*`) track
spans only (backward compatible). HTTP-level counters
(`http_2xx`, `http_4xx`, `http_5xx`, `network_err`) are global
across all signals.

### Added — Property tests

`tests/property/test_property_async_metrics.c` (4 properties):
- `prop_async_metric_sent` — emit + tick + verify sent_metrics.
- `prop_async_log_sent` — same for logs.
- `prop_async_spans_coexist` — spans and metrics flow through
  the same exporter without interference.
- `prop_async_metric_drop_full` — queue overflow returns
  BUFFER_FULL and increments dropped_metrics_full.

## [0.5.27] - 2026-08-09

Header accuracy audit + emit throughput benchmark + compile-time
span cap overrides.

### Fixed — Stale metric.h header comments

`include/otlp-c/metric.h` had three stale claims from the v0.4 era:
- "Three metric types are supported in v0.4" → four types are
  supported in v0.5.x (Counter, Gauge, Histogram,
  ExponentialHistogram).
- "ExponentialHistogram and Summary are deferred" →
  ExponentialHistogram IS supported (since v0.5.x); only Summary
  is not (the OTel spec recommends Histogram/ExpHistogram for new
  code; Summary is legacy).
- "Counter ... is_monotonic=true, cumulative temporality" → both
  are now configurable (v0.5.26).

Also fixed `src/span_internal.h`: claimed "v0.5 supports name +
time only; attributes are deferred" for Span.Event, but events
DO have attributes (up to `OTLP_EVENT_MAX_ATTRS`).

### Added — Compile-time span cap overrides

The span/event/link attribute caps (`OTLP_SPAN_MAX_ATTRIBUTES`,
`OTLP_SPAN_MAX_EVENTS`, `OTLP_SPAN_MAX_LINKS`,
`OTLP_EVENT_MAX_ATTRS`, `OTLP_LINK_MAX_ATTRS`) are now guarded
with `#ifndef`. Callers who need more (or fewer) slots can
override at compile time without redefinition warnings:

```sh
cmake -DCMAKE_C_FLAGS="-DOTLP_SPAN_MAX_ATTRIBUTES=256" ...
```

Defaults are unchanged (128/64/64/32/32). The `#ifndef` guard is
the standard C pattern for compile-time configurability — OCP at
the preprocessor level.

### Added — Emit throughput benchmark

`bench/bench_emit.c` — measures the full emit pipeline: span
clone, MPSC push, tick drain, protobuf encode, null_transport
"send". Isolates the library's internal cost from network I/O.

Typical results (Apple M-series, null_transport):
- 1000 spans, 0 attrs: ~29 μs/op, ~35K spans/sec
- 5000 spans, 5 attrs: ~25 μs/op, ~40K spans/sec

Registered in `bench/CMakeLists.txt` as `otlp_bench_emit`. Opt-in
via `-DOTLP_C_BUILD_BENCH=ON`.

### Fixed — Stale encoder call in bench_encode.c

`bench_encode.c` still used the v0.5.19 encoder signature
(`otlp_encode_export_trace_service_request` without the
`resource_attributes` params added in v0.5.20). Fixed. Also
removed an unused helper function (`-Wunused-function`).

## [0.5.26] - 2026-08-09

Configurable metric aggregation temporality + is_monotonic —
fixes hardcoded encoder values that limited metric semantics.

### Fixed — Aggregation temporality was always CUMULATIVE

The OTLP encoder hardcoded `aggregation_temporality = CUMULATIVE`
for Counter (Sum), Histogram, and ExponentialHistogram. Callers
who needed DELTA temporality (push-based delta reporting, common
in Prometheus-style scraping) had no way to set it. The field
was defined in the header (`OTLP_AGG_TEMP_DELTA = 1`,
`OTLP_AGG_TEMP_CUMULATIVE = 2`) but the metric struct didn't
store it and the encoder never read it.

Now configurable:

```c
otlp_metric_set_aggregation_temporality(m, OTLP_AGG_TEMP_DELTA);
```

Default remains CUMULATIVE (backward compatible). The setter
validates the value is DELTA or CUMULATIVE; UNSPECIFIED is
rejected.

### Fixed — is_monotonic was always true for Counter

The encoder hardcoded `is_monotonic = true` for Counter (Sum).
Callers who needed an up/down counter (queue depth, active
connections — metrics that can decrease) had no way to set
`is_monotonic = false`.

Now configurable:

```c
otlp_metric_set_monotonic(m, false);
```

Default remains true (backward compatible). Note: proto3 wire
encoding omits `is_monotonic` when false (the zero value); the
collector interprets absence as false. This is correct proto3
semantics.

### Added — Property tests

Two new properties in `test_property_metrics.c` (was 6, now 8):

- `prop_metrics_delta_temporality` — encodes a counter with
  DELTA temporality; verifies field 2 (agg_temp) on the wire
  has value 1 (DELTA).
- `prop_metrics_non_monotonic_counter` — encodes a counter
  with `is_monotonic = false`; verifies field 3 is ABSENT
  (proto3 omits false/default bools).

## [0.5.25] - 2026-08-08

HTTP connect/read timeout enforcement — fixes dead configuration
that was documented as functional but did nothing.

### Fixed — connect_timeout_ms / read_timeout_ms were dead config

The exporter opts `connect_timeout_ms` (default 5000) and
`read_timeout_ms` (default 10000) were normalized in
`otlp_exporter_create` but **never stored in the exporter struct
or passed to the HTTP client**. The HTTP state machine had no
concept of timeouts — it polled forever until the TCP stack gave
up (typically 60-120 seconds for connect, indefinite for read).

Impact: if the collector was unreachable, tick() blocked for up
to `flush_timeout_ms` (30s default) per failed request. With
this fix, the HTTP client now enforces the configured deadlines:
connect timeout fires after `connect_timeout_ms`, read timeout
fires after `read_timeout_ms` of inter-recv silence.

### Added — Deadline enforcement in HTTP state machine

`otlp_http_request_start` and `otlp_http_request_start_with_socket`
(internal API) now accept `connect_timeout_ms` and
`read_timeout_ms` parameters. 0 means no timeout (infinite) —
used by tests and by callers that have their own deadline logic.

The request struct stores the durations + a monotonic start time.
`step_connecting` checks the connect deadline; `step_reading`
checks the inter-recv deadline (reset on each successful recv so
a slow-but-steady stream doesn't time out). On timeout, the
request transitions to FAILED and returns `OTLP_ERR_TIMEOUT`.

**Timing subtlety:** the deadline clock starts AFTER
`getaddrinfo` + `connect` initiation, not at function entry.
The blocking DNS lookup can take seconds; measuring from before
it would make the deadline fire prematurely.

### Wired — Exporter opts through to HTTP

The exporter now stores `connect_timeout_ms` and
`read_timeout_ms` and passes them through
`otlp_exporter_otel_build_request` (traces) and `flush_sync`
(metrics/logs) to the HTTP client. All 9 call sites updated.

### Added — Timeout property test

`tests/property/test_property_http_timeout.c`: starts a request
to `192.0.2.1` (RFC 5737 TEST-NET-1, IANA-reserved, never
routed) with `connect_timeout_ms=200`. Asserts the request
reaches FAILED within 5 seconds — verifying bounded completion
rather than the 60+ second TCP default. POSIX-only (uses
`clock_gettime`).

## [0.5.24] - 2026-08-08

Typed Resource attributes — completes the Resource feature
shipped string-only in v0.5.20. OTLP semantic conventions define
Resource attributes as int (`process.pid`, `host.cpu.count`),
double (`system.memory.utilization`), and bool
(`cloud.auto_scale`) in addition to the common string case.

### Added — Typed Resource attribute values

New public enum + struct fields (source-compatible):

```c
typedef enum {
    OTLP_RESOURCE_ATTR_STRING = 0,  /* default — backward compat */
    OTLP_RESOURCE_ATTR_INT64  = 1,
    OTLP_RESOURCE_ATTR_DOUBLE = 2,
    OTLP_RESOURCE_ATTR_BOOL   = 3,
} otlp_resource_attr_type_t;

typedef struct {
    const char *key;
    const char *value;   /* used when type == STRING (default) */
    otlp_resource_attr_type_t type;  /* 0 = STRING */
    int64_t int64_val;   /* used when type == INT64 */
    double  double_val;  /* used when type == DOUBLE */
    bool    bool_val;    /* used when type == BOOL */
} otlp_resource_attr_t;
```

**Source-level backward compatibility:** existing callers who
write `{.key = "k", .value = "v"}` need no changes — `.type`
defaults to 0 (STRING) and `.value` is used as before. The
struct grew (new fields appended), so it's not binary-compatible;
within the 0.x line this is allowed per CLAUDE.md.

**Encoder dispatch:** `otlp_emit_resource` maps the public type
enum to the internal `otlp_attr_type` enum, then the existing
table-driven `attr_encoders[]` dispatch (from v0.5.7) handles
the wire encoding. Adding a new value type is one enum entry +
one table row — OCP.

**Exporter deep-copy:** `otlp_exporter_create` now copies the
type + all value fields. `.value` is always copied for STRING
attrs; for other types it may be NULL (the free path handles
both uniformly via `otlp_free(NULL)` which is a no-op).

### Added — Typed-value property tests

3 new properties in `tests/property/test_property_resource_attrs.c`
(was 4, now 7):

- `prop_resource_typed_int64` — `process.pid = 4242` (INT64)
  appears on the wire; service.name still present (backward
  compat).
- `prop_resource_typed_bool` — `cloud.auto_scale = true` (BOOL)
  appears on the wire.
- `prop_resource_mixed_types` — string + int64 + bool + double
  all coexist in one Resource.

Uses a shared `find_key` helper that walks the wire to verify a
given key is present at the Resource level. The exact value-byte
encoding is covered by the existing AnyValue encoder tests; this
test verifies the resource encoder dispatches types correctly.

### Fixed — Existing resource-attr test uninitialized fields

The v0.5.20 tests declared `otlp_resource_attr_t attrs[3];`
without initialization. Before v0.5.24, the struct had only
`key` + `value` (both explicitly set), so uninitialized fields
didn't matter. After v0.5.24, the struct has `type` +
`int64_val` etc. — uninitialized `.type` could be garbage,
breaking the STRING dispatch. Fixed with `memset(attrs, 0,
sizeof(attrs))` in each test.

## [0.5.23] - 2026-08-08

Diagnostic callback for production observability + a critical
MPSC queue data-loss fix that the diagnostic feature uncovered.

### Fixed — MPSC queue never enforced capacity (silent data loss)

The bounded MPSC queue's sequence-number formulas were wrong:
- Push released `seq = h + capacity + 1` (should be `h + 1` per
  the canonical Vyukov scheme).
- Init stored `seq = i + 1` (should be `i`).
- Pop expected `seq == t + capacity + 1` (should be `t + 1`).

Result: the producer's wrap-around full check (`diff < 0`) NEVER
triggered. The queue claimed to be bounded but silently
overwrote unconsumed spans whenever the consumer (tick) couldn't
keep up. No error, no backpressure — just data loss.

The bug was present since the queue was written (early v0.5.x)
but not caught because:
- Tests always had the consumer keeping up (concurrency stress
  test drains via tick).
- No diagnostic surfaced "I dropped a span because the queue
  was full" — the code path was dead.

The diagnostic callback test (below) exposed it: emitting 20
spans into a capacity-4 queue with no ticking returned OK for
all 20 instead of `OTLP_ERR_BUFFER_FULL` for the last 16.

Fix: restored the canonical Vyukov scheme — init `seq = i`,
push checks `diff = seq - h` and releases `seq = h + 1`, pop
checks `diff = seq - (t + 1)` and releases `seq = t + capacity`.
Now the full check fires correctly on wrap-around; emit returns
`OTLP_ERR_BUFFER_FULL` when the queue is actually full.

### Added — Diagnostic callback (`otlp_exporter_set_logger`)

Optional callback the library invokes at notable events. Gives
the caller real-time visibility into exporter behavior for
production debugging — the stats counters tell you WHAT
happened after the fact; this tells you WHY.

```c
typedef enum {
    OTLP_LOG_DEBUG,  /* routine operation (batch sent) */
    OTLP_LOG_INFO,   /* notable but expected (retry armed) */
    OTLP_LOG_WARN,   /* degraded operation (queue full, transient retry) */
    OTLP_LOG_ERROR,  /* unexpected failure (max retries, permanent 4xx) */
} otlp_log_level_t;

typedef void (*otlp_log_fn)(void *ctx, otlp_log_level_t level,
                             const char *message);

void otlp_exporter_set_logger(otlp_exporter_t *exp,
                               otlp_log_fn fn, void *ctx);
```

Wired at 7 events in the exporter:
- emit/emit_move queue full → WARN
- record_outcome network error → retry → WARN
- record_outcome network error → max retries → ERROR
- record_outcome 5xx → retry → WARN
- record_outcome 5xx → max retries → ERROR
- record_outcome 4xx permanent → ERROR
- record_outcome 2xx success → DEBUG

Thread-safety: the callback may fire from any thread that
touches the exporter. The implementation MUST be thread-safe.
Default (no callback): every log site compiles to a NULL-pointer
check — zero observable overhead.

### Added — Diagnostic property tests

`tests/property/test_property_diagnostics.c` (4 properties):
- `prop_diag_fires_on_queue_full` — 20 emits into capacity-4
  queue fires WARN with "queue full". (This is the test that
  caught the MPSC bug.)
- `prop_diag_fires_on_4xx_permanent` — null_transport returning
  404 fires ERROR with "permanent".
- `prop_diag_fires_on_success` — successful send fires DEBUG
  with "batch sent".
- `prop_diag_disabled_by_default` — no callback = no crash, no
  hang. Exercises the NULL-check zero-overhead path.

## [0.5.22] - 2026-08-08

W3C propagation completeness — baggage support + DRY extraction
of the traceparent formatting primitive.

### Added — W3C Baggage propagation

The library now propagates the W3C [Baggage](https://www.w3.org/TR/baggage/)
header alongside traceparent and tracestate. Baggage carries
arbitrary key-value pairs (user IDs, request IDs, feature flags)
across service boundaries — distributed tracing without baggage
is incomplete.

**Public API** (additive — existing callers see no behavior change
when baggage is empty):

- `OTLP_CONTEXT_BAGGAGE_MAX` (2048) — max baggage string length.
- `otlp_context_t.baggage[OTLP_CONTEXT_BAGGAGE_MAX]` — opaque
  string field, same contract as `tracestate` (library doesn't
  parse it; caller formats/reads).
- `OTLP_CONTEXT_BAGGAGE_HEADER` — the string `"baggage"`.
- `otlp_context_inject` now writes the baggage header when
  `ctx.baggage` is non-empty.
- `otlp_context_extract` now reads the baggage header when present.

OCP: the field is additive; existing callers that don't set
baggage see identical behavior (no header written, no field
populated).

### Added — `otlp_traceparent_format_raw` primitive

The traceparent hex-formatting logic was duplicated between
`src/w3c.c` (`otlp_traceparent_format`, which takes a span) and
`src/context.c` (`otlp_context_inject`, which has raw IDs from
the context struct and inlined its own copy of the formatting).

Extracted the raw-bytes version as a new public primitive:

```c
otlp_status_t otlp_traceparent_format_raw(
    const uint8_t trace_id[16],
    const uint8_t span_id[8],
    bool sampled,
    char *buf, size_t cap,
    size_t *out_len);  /* optional; may be NULL */
```

- `otlp_traceparent_format` (span-based) now delegates to it.
- `otlp_context_inject` calls it directly, removing ~20 lines
  of duplicated hex formatting.
- `out_len` is now optional (NULL means "don't care") — aligns
  with the span-based wrapper, which also accepts NULL.

This is both DRY (eliminates duplication) and API completion (the
raw-bytes version is the fundamental operation; the span-based
version is a convenience wrapper).

### Added — Baggage + DRY property tests

`tests/property/test_property_baggage.c` (5 properties):

- `prop_baggage_roundtrip` — inject with baggage, extract,
  baggage matches.
- `prop_baggage_absent_on_extract` — carrier without baggage
  header produces empty baggage field.
- `prop_baggage_with_tracestate` — both baggage and tracestate
  coexist on the same carrier.
- `prop_baggage_header_constant` — `OTLP_CONTEXT_BAGGAGE_HEADER`
  is the string `"baggage"`.
- `prop_format_raw_matches_format` — `otlp_traceparent_format_raw`
  produces the same output as `otlp_traceparent_format` for a
  given span (DRY regression check).

## [0.5.21] - 2026-08-08

Configurable flush timeout + multi-threaded example + a
null-transport backoff fix that makes retry testing meaningful.

### Added — `flush_timeout_ms` on exporter opts

The `flush()` and `flush_metric()` / `flush_log()` paths had a
hardcoded 30-second cap (flagged as LOW finding #3 in
SECURITY-ASSESSMENT.md). Now configurable:

```c
otlp_exporter_opts_t opts = { 0 };
opts.flush_timeout_ms = 5000;  /* 5s cap; default remains 30000 */
```

Closes SECURITY-ASSESSMENT.md LOW finding #3. Callers wanting
unbounded flush should loop `tick()` manually (documented in the
opts field comment).

### Fixed — null_transport ignores backoff_armed

The null-transport fast path in `tick()` fired on every tick
regardless of `backoff_armed`. This meant the null_transport
status callback (used to test retry/backoff behavior) exhausted
retries instantly — the callback fired `max_retries + 1` times in
microseconds, and the batch was dropped before any backoff logic
ran. Retry tests via null_transport were effectively meaningless.

Fix: the null-transport path now checks `!e->backoff_armed`
before firing, matching the real HTTP path's behavior. Retry/
backoff is now testable deterministically via the status callback.

### Changed — `flush_sync()` converted from iteration-count to time-based

The synchronous metric/log flush path used `for (i = 0; i < 30000; i++)`
with a 1ms sleep per iteration — an iteration-count proxy for 30
seconds. Replaced with an explicit deadline check using
`flush_timeout_ms`. Cleaner (no magic iteration count) and respects
the configured timeout.

### Added — Multi-threaded example

`examples/multithread.c`: N worker threads emit spans concurrently
into one exporter while a dedicated tick thread drains the queue.
Demonstrates the library's core embedding pattern: thread-safe
`emit()` from any thread + caller-driven `tick()` from one thread.
Cross-platform (pthread on POSIX, CreateThread on Windows). Runs
via null_transport so it works without a local collector.

### Added — Flush timeout property test

`tests/property/test_property_flush_timeout.c`: verifies a custom
`flush_timeout_ms` (200ms) is respected — flush returns near the
configured deadline, not the 30s default. Uses null_transport with
a 500-status callback + high `backoff_initial_ms` to keep pending
non-empty without exhausting retries.

## [0.5.20] - 2026-08-08

Resource attributes — the OTLP Resource message carries arbitrary
KeyValue attributes (service.version, deployment.environment,
host.name, etc.) alongside service.name. Until now the library only
let callers set service.name; now the full Resource is exposed.

### Added — Resource attributes on the public API

New public type in `include/otlp-c/exporter.h`:

```c
typedef struct {
    const char *key;
    const char *value;
} otlp_resource_attr_t;
```

New opts fields on `otlp_exporter_opts_t`:

```c
const otlp_resource_attr_t *resource_attributes;
size_t n_resource_attributes;
```

`service.name` (from the existing `service_name` field) is always
emitted first; entries in `resource_attributes` follow in array
order. Empty-key or empty-value entries are skipped (matches the
protobuf "empty fields omitted" convention the library uses
elsewhere). v0.5.x supports string values only — covers every
common Resource attribute. A typed variant (int/double/bool/bytes)
can be added later without breaking this struct.

### Changed — Internal encoder signatures

`otlp_emit_resource`, `otlp_encode_export_trace_service_request`,
`otlp_encode_export_metrics_service_request`, and
`otlp_encode_export_logs_service_request` now take
`(const otlp_resource_attr_t *attrs, size_t n_attrs)` alongside
`service_name`. All three signal encoders (traces, metrics, logs)
emit the same Resource. The exporter deep-copies the attrs array
at `otlp_exporter_create` and frees it at `otlp_exporter_free`.

This is an INTERNAL API change (in `src/otlp_messages.h`); the
PUBLIC API change is purely additive (new opts fields, no existing
field changed).

### Added — Property test for resource attributes

New `tests/property/test_property_resource_attrs.c` (4 properties):

- `prop_resource_empty` — no service + no attrs → 0 bytes.
- `prop_resource_service_name_only` — service.name on wire.
- `prop_resource_extra_attrs_encoded` — 3 extra attrs all present
  alongside service.name.
- `prop_resource_attrs_skip_empty` — empty-key/empty-value entries
  omitted; service.name still present.

Uses the shared `walker.h` to descend the wire tree and scan the
Resource's KeyValue list for each expected key/value pair.

## [0.5.19] - 2026-08-08

Test infrastructure TSAN races fixed + zero compiler warnings. The
TSAN CI job added in v0.5.15 flagged three tests as data races; all
three shared the same root cause and are now fixed.

### Fixed — TSAN data races in test infrastructure

Three tests failed intermittently under the v0.5.15 TSAN job, all
from one root cause: cross-thread shared state in test helpers
accessed without atomics, synchronized only by `nanosleep` (which
is NOT a synchronization primitive).

- `tests/test_helper_echo.{h,c}`: `running`, `requests_served`,
  `requests_seen` are now `otlp_atomic_int` / `otlp_atomic_u64`
  via `../src/atomic_compat.h`. Memory ordering: the worker's
  `running = 0` store uses RELEASE; `echo_server_join`'s poll loop
  uses ACQUIRE, which establishes happens-before for all
  pre-exit writes (so post-join reads of `requests_served` are
  safe with RELAXED loads).
- `tests/property/test_property_keepalive.c`: `mini_srv.requests_served`
  atomicized. Also reordered the increment to happen BEFORE `send()`
  (logical correctness — once main's `recv()` returns the response,
  the counter has already advanced).
- `tests/test_concurrency_stress.c`: `srv.requests_served` reads
  converted to `otlp_atomic_load_u64`.

CI now passes the full TSAN matrix cleanly: 27/27 tests, zero race
reports. Local reproduction confirmed before and after the fix.

### Fixed — Pre-existing `-Wcomment` warning

`src/internal_util.h:14` had the sequence `/*` inside a block
comment (in the phrase "src/*.c files"). clang's `-Wcomment`
flagged this as a potential nested-comment error since v0.4.
Rephrased to "source .c files under src/".

### Fixed — Two `-Wunused` warnings in tests

- `test_exporter_echo.c`: dead `static int requests_seen` counter
  in `count_handler` — incremented but never read. Removed.
- `test_property_seed.c`: `prop_version_consistent(uint64_t seed)`
  had an unused `seed` parameter (the property doesn't need
  randomness — it checks a constant). Marked `(void) seed;`.

Result: zero compiler warnings across plain, ASAN, UBSAN, and TSAN
builds with the project's full warning set (`-Wall -Wextra
-Wpedantic -Wconversion -Wsign-conversion -Wundef -Wshadow
-Wpointer-arith -Wformat=2 -Wwrite-strings -Wold-style-definition
-Wmissing-prototypes`).

## [0.5.19] - 2026-08-08

Policy-docs staleness sweep — the same kind of accuracy audit
[0.5.16] did for CLAUDE.md, applied to the rest of the policy
surface.

### Fixed — SECURITY.md concurrency-surface claim

Listed "race conditions in the exporter's **background thread**" as
in scope. The library has had no background thread since the
caller-tick exporter landed early in the v0.5.x line. Replaced with
the correct surface (MPSC queue, atomic stats, tracer's lock-free
PRNG) and a pointer to `docs/deployment.md`.

### Fixed — SECURITY.md hardening section missing TSAN

Recommended ASAN + UBSAN but omitted TSAN. The CI runs all three
(added in v0.5.15). Added `-DOTLP_C_ENABLE_TSAN=ON` to the
recommendation.

### Fixed — SECURITY-ASSESSMENT.md v0.1.x → v0.5.x scope

The assessment was tagged "v0.1.x" but the project was at v0.5.17.
Refreshed: added surface sections for metrics, logs, context
propagation, sampler, and slab allocator; added a threat-model note
for `otlp_install_slab_allocator` (the address-range check that
catches hostile callers freeing non-slab pointers); marked the
completed v0.2.x recommendations with their resolutions.

### Fixed — README badge URL

Pointed at `workflows/build.yml` (renamed to `workflows/ci.yml` in
an earlier release). Badge SVG was 404; visitors saw a broken/red
build status. Fixed.

### Fixed — README platform coverage

Listed OpenBSD and NetBSD as supported alongside Linux/macOS/Windows.
CI does not run on OpenBSD or NetBSD. Reworded to distinguish
"CI'd" (Linux, macOS, FreeBSD best-effort, Windows) from "expected
to work on any POSIX platform".

### Fixed — README status version

"**0.5.10.**" → "**0.5.18.**" (this release tags 0.5.19).

## [0.5.17] - 2026-08-08

Zero compiler warnings. Stale comments cleaned.

### Fixed — -Wmissing-prototypes warning

`otlp_version()` was declared in `otlp.h` but defined in `common.c`
which includes only `version.h`. Moved the declaration to `version.h`
where it logically belongs. Now `-Wmissing-prototypes` is clean.

### Fixed — Stale comments

- `src/common.c`: removed "Stub implementations" comment (no stubs
  exist since v0.1.0).
- `src/platform.c`: removed "minimal stub for Phase 0" and
  "close enough for stub" comments.

Result: clean build with `-Wall -Wextra -Wpedantic -Wconversion
-Wsign-conversion -Wmissing-prototypes`. Zero warnings.

## [0.5.16] - 2026-08-08

CLAUDE.md accuracy audit — the most important documentation fix.

### Fixed — CLAUDE.md stale claims

The project's CLAUDE.md (the file every future contributor and AI
agent reads first) had 5 stale claims from the v0.1.0 bootstrap era:

- "emits trace spans (and, in future, metrics and logs)" → corrected
  to "all three signals"
- "Stubbed-default builds... stub library" → corrected to "Clean
  default builds... no stubs"
- Phase 1-8 instructions as future work → replaced with completion
  status + OCP extension guide
- "The stubs in src/*.c are placeholders" → removed
- "the exporter holds a mutex" → corrected to "lock-free"

Key files table updated with all current modules (was missing
metric.h, log.h, sampler.h, context.h, slab.h, otlp_schema.h, etc.).

## [0.5.15] - 2026-08-08

Complete sanitizer trio in CI: ASAN + UBSAN + TSAN.

### Added — UBSAN CI job

Builds with `-DOTLP_C_ENABLE_UBSAN=ON`, runs full test suite.
Catches integer overflow, null dereference, alignment, and other
undefined behavior. Verified locally: 27/27 tests clean.

### Added — TSAN CI job

Builds with `-DOTLP_C_ENABLE_TSAN=ON`, runs full test suite.
Catches data races in the MPSC queue, tracer PRNG atomic CAS,
and exporter stats counters. Validates lock-free correctness.

The project now has complete sanitizer coverage in CI:
ASAN (memory safety) + UBSAN (undefined behavior) + TSAN (data races).

## [0.5.14] - 2026-08-08

ASAN CI + vcpkg port sync + ExpHistogram setter test.

### Added — AddressSanitizer CI

New `asan` job in CI: builds with ASAN on Ubuntu 24.04, runs full
test suite with leak detection. Catches memory safety issues that
property tests alone might miss.

### Fixed — vcpkg overlay port

Updated from stale 0.3.0 to 0.5.14 (version + REF in portfile.cmake).

### Added — ExpHistogram setter test

New property test verifying `otlp_metric_set_exp_histogram()` end-to-end:
creates metric, sets scale + positive buckets, flushes via null_transport.

## [0.5.13] - 2026-08-08

Slab performance fix + ExpHistogram setter + benchmark.

### Fixed — Slab allocator: O(1) free-list

The slab's `otlp_slab_alloc` used a linear scan over the `used[]`
bitmap — O(capacity) per allocation. Benchmark showed 13× slower
than system malloc (429 ns/op vs 32 ns/op).

Replaced with a free-list stack: alloc pops (O(1)), free pushes
(O(1)). Benchmark now shows 36 ns/op — near-parity with optimized
system malloc.

Also fixed an infinite-recursion bug in the alloc/free fallback
paths when the slab is installed as the global allocator.

### Added — Slab benchmark

`bench/bench_slab.c`: 100K alloc+free cycles of 64-byte objects.
Measures ns/op for system malloc vs slab allocator. Prints speedup.

### Added — ExponentialHistogram setter

`otlp_metric_set_exp_histogram()`: sets scale + positive/negative
bucket data in one call. The library copies the arrays. Caller
manages bucket-index computation.

## [0.5.12] - 2026-08-08

Architecture docs + cookbook updated for v0.5.x.

### Changed — Architecture docs

`docs/architecture.md` comprehensively rewritten: updated the layered
view to show all 21 modules (was traces-only with 8 modules), added
MECE table with 20 rows (was 8), added "Design patterns" section
documenting model-driven encoding, table-driven metric dispatch,
caller-driven I/O, and lock-free MPSC. Fixed stale claims: mutex →
lock-free, arena → slab, traces-only → all three signals.

### Added — Cookbook patterns

`docs/cookbook.md` extended with sections 6-10: metric counter +
histogram patterns, structured logs with trace correlation, context
propagation across processes (inject/extract), custom sampling
(ratio + always-off), and custom allocator/slab integration.

## [0.5.11] - 2026-08-08

README updated to reflect the v0.5.x API surface.

### Changed — README

- Status updated from "0.1.0 (alpha)" to "0.5.10".
- Description updated to mention all three signals (traces, metrics,
  logs) instead of just traces.
- New "Features" section listing all capabilities.
- Platform support list updated.

## [0.5.10] - 2026-08-07

Property test coverage for flush + docs for all signals.

### Added — Flush property tests

`test_property_flush.c` (3 properties):
- `prop_flush_metric_null_transport` — counter flush returns OK.
- `prop_flush_log_null_transport` — log flush returns OK.
- `prop_flush_metric_variants` — all 4 metric types (counter, gauge,
  histogram, exp-histogram) flush without error.

Tests use null_transport mode — no echo server, deterministic.

### Changed — Quickstart docs

`docs/quickstart.md` now includes code samples for metrics, logs,
context propagation, sampling, and custom allocator — in addition
to the existing traces example. Reflects the full v0.5.x API.

## [0.5.9] - 2026-08-07

Exporter now exports all three signals. Examples show full API.

### Added — TODO 51: Exporter metric/log flush

- `otlp_exporter_flush_metric(exp, metric)` — synchronously encodes
  and POSTs one metric to `/v1/metrics`.
- `otlp_exporter_flush_log(exp, log)` — synchronously encodes and
  POSTs one log record to `/v1/logs`.
- URL derived from exporter's endpoint by replacing path component.
- Null-transport mode: returns OK immediately.
- Uses existing HTTP infrastructure; no new dependencies.

### Changed — Example updated

`examples/minimal.c` now demonstrates the full v0.5.x API: span with
attributes + events, metric counter, log record, context propagation
(traceparent), ratio sampler. Runs standalone via null_transport.

## [0.5.8] - 2026-08-07

Code quality cleanup — DRY completion and API surface completeness.

### Fixed — DRY: walker.h fully wired

The shared test walker (`tests/property/walker.h`) was created in
v0.5.7 but only wired into `test_property_metrics.c`. Two other test
files (`test_property_logs.c`, `test_property_events_context.c`)
still had inline copies of `find_at_level` and `descend`. Now all
three use the shared header. Zero duplication.

### Fixed — API completeness: allocator.h in umbrella

`include/otlp-c/allocator.h` (the custom allocator hook API:
`otlp_set_allocator`, `otlp_get_allocator`) was missing from the
umbrella header `otlp.h`. Callers who `#include <otlp-c/otlp.h>`
now get the allocator API without a separate include.

## [0.5.7] - 2026-08-07

All 26 tests pass with zero flakes. Zero continue-on-error in CI.

### Added — Null-transport status callback

`otlp_exporter_set_null_transport_status_fn(exp, fn, ctx)` lets tests
control the HTTP status code returned by each null-transport "send".
Default is 200. The callback is called per batch, receiving opaque
`ctx`. Enables deterministic retry/failure testing without threads.

### Changed — exporter-retry test rewritten

The retry test was the last test using the threaded echo server +
`RUN_SERIAL`. Rewritten to use null_transport with a status callback
that returns 500 on first call, 200 after (case 1: retry success)
and 404 always (case 2: permanent failure). No echo server, no
threads, no POSIX guard. Runs on all platforms including Windows.

### Fixed — Windows CMake find_package

Replaced `${{ github.workspace }}` (Windows backslashes mangled by
bash) with `$GITHUB_WORKSPACE` (forward slashes, bash-compatible) in
the CI consumer test. Removed `continue-on-error` for the Windows
CMake find_package entry.

### Added — DRY test walker

Extracted `walker_find_at_level` + `walker_descend` from 4 duplicated
copies across test files into a shared `tests/property/walker.h`.
Reduces test boilerplate.

## [0.5.6] - 2026-08-07

Eliminates the property-exporter test flake. CI is now fully clean.

### Added — TODO 50: Null-transport mode for deterministic tests

`otlp_exporter_set_null_transport(exp, true)` makes the exporter
skip all HTTP I/O and immediately mark batches as "sent" (200 OK).
Used by property tests to eliminate the threaded echo server that
was the root cause of timing flakes and SEGFAULTs.

The property-exporter test is rewritten to use null_transport: no
echo server, no threads, no timing sensitivity. Runs 1000 iterations
deterministically on every platform, including Windows.

This is a simpler approach than the full transport-interface refactor
described in TODO 50's original spec. The null_transport mode is
sufficient for batching-behavior tests; the full transport interface
(for pluggable UDP/shared-memory/etc.) remains a future design.

### Changed

- `property-exporter` test no longer POSIX-only: runs on Windows too.
  No longer requires `test_helper_echo.c` or `Threads::Threads`.
- Removed `RUN_SERIAL` from `property-exporter` in CMakeLists.txt.
- Removed `-E 'property-exporter'` exclusion from CI test steps.
- Removed the separate `continue-on-error` test step for the flaky
  exporter test in both main CI and Alpine CI.
- `struct otlp_exporter` extended with `bool null_transport` field.

## [0.5.5] - 2026-08-07

ExponentialHistogram encoder completed. The last standard metric type.

### Added — TODO 46: ExponentialHistogram (full)

- `OTLP_METRIC_EXP_HISTOGRAM` type: `record()` increments count +
  sum + zero_count.
- Encoder: `emit_exp_histogram_data_point` emits attributes,
  start_time, time, count, sum, scale (zigzag sint32), zero_count,
  positive/negative `ExponentialHistogramBuckets` (offset zigzag
  + packed bucket_counts), via the table-driven dispatch.
- Schema tables: `OTLP_EHDP_FIELDS[]` (10 fields),
  `OTLP_EHB_FIELDS[]` (2 fields).
- Zigzag encoding for `sint32` scale and offset (proto wire
  compatibility).
- Dispatch table entry in `metric_kind_specs[]`.
- `struct otlp_metric` extended with `exp_scale`,
  `exp_zero_count`, `exp_pos_offset`, `exp_pos_counts`,
  `exp_neg_offset`, `exp_neg_counts` + accessors.

### Fixed — TODO status text

TODOs 47, 48, 49 updated from "Spec only" to "Complete (v0.5.4)"
— they were shipped in v0.5.4 but the status text was stale.

## [0.5.4] - 2026-08-07

Architectural completion — four deferred TODOs implemented.

### Added — TODO 49: Slab integration

`otlp_install_slab_allocator(slot_size, capacity)` wraps the existing
slab allocator via the `otlp_set_allocator` hook. All subsequent
`otlp_malloc`/`otlp_free` calls route small allocations through the
slab arena; oversize and overflow fall through to the previous
allocator. `otlp_uninstall_slab_allocator` restores the previous
allocator and frees the arena.

Fixed an infinite-recursion bug in the free hook: `otlp_slab_free_ptr`
falls through to `otlp_free` for non-arena pointers, which re-enters
the hook. The hook now inlines the arena address-range check.

### Added — TODO 48: tracestate in SpanContext

`otlp_context_t` now carries a `tracestate[512]` field (raw W3C
tracestate header value). `otlp_context_inject` emits both
`traceparent` and `tracestate` headers (if non-empty).
`otlp_context_extract` reads both headers. The library treats
tracestate as opaque — the caller formats/parses the
`key=value,key=value` list.

### Added — TODO 47: Event/Link attributes

- `struct otlp_event` extended with `attrs[32] + n_attrs`.
- `struct otlp_link` extended with `attrs[32] + n_attrs`.
- New public API: `otlp_span_set_event_attribute_string(span, key,
  value)` and `otlp_span_set_link_attribute_string(span, key, value)`.
  These set attributes on the most-recently-added event/link.
- The traces encoder now emits Event.attributes (field 3) and
  Link.attributes (field 4) via `otlp_emit_attributes`.
- `otlp_span_free` recursively frees event/link attributes.

### Added — TODO 46: ExponentialHistogram (partial)

- `OTLP_METRIC_EXP_HISTOGRAM` enum value added.
- Schema entry: `exponential_histogram` at field 10 of Metric.
- The full encoder (positive/negative buckets, scale, zero_count)
  is deferred — the schema slot is reserved so adding the encoder
  later is purely additive (OCP).

### Changed

- `otlp_context_t` is now ~540 bytes (was 28). Still pass-by-value;
  the tracestate field is inline (no heap allocation per context).

## [0.5.3] - 2026-08-07

Architectural completion + install-path fix.

### Added — AnyValue variants (OCP gap closed)

- `OTLP_ATTR_ARRAY` and `OTLP_ATTR_KVLIST` AnyValue variants
  added to the attribute type enum and union (`src/span_internal.h`).
  The AnyValue oneof dispatch table in `otlp_messages.c` is now
  fully populated — all seven proto variants have encoder functions.
  Schema tables for `ArrayValue{1}` and `KeyValueList{1}` added to
  `otlp_schema.h`. Recursive: array items can themselves be
  array/kvlist.
- `otlp_attribute_free(struct otlp_attribute *)`: recursive free
  that handles owned strings, bytes, and nested arrays/kvlists.
  In `internal_util.c`.

### Fixed

- **Linux/macOS CMake `find_package` install-path**: pinned
  `CMAKE_INSTALL_LIBDIR` to `"lib"` before `include(GNUInstallDirs)`
  so the cmake config files land at `<prefix>/lib/cmake/otlp-c/`
  on every platform. Previously, GNUInstallDirs chose
  arch-suffixed paths on some platforms, breaking consumer
  `find_package(otlp-c CONFIG)` calls.
- Windows CMake find_package still gated with `continue-on-error` —
  the install path is now correct but the consumer-test step has a
  bash-on-Windows path-mangling issue with `CMAKE_PREFIX_PATH`.
  Tracked separately.

### Specs

Five new TODO files documenting deferred architectural work, each
with full design notes (not just goals):

- `TODO.complete/46-exponential-histogram.md` — the last standard
  metric type. Schema entries + dispatch table slot reserved.
- `TODO.complete/47-event-link-attributes.md` — builder-pattern API
  for events + links with attributes.
- `TODO.complete/48-tracestate-in-context.md` — `otlp_context_t`
  carrying up to 32 vendor tracestate entries.
- `TODO.complete/49-slab-integration.md` — wire slab into global
  allocator via `otlp_install_slab_allocator`.
- `TODO.complete/50-deterministic-test-transport.md` — mock HTTP
  transport interface to fix property-exporter flake.

These are spec-only for v0.5.x. Each has acceptance criteria so
the implementation work is well-scoped.

## [0.5.2] - 2026-08-07

CI / runner hygiene release. No code changes; same library as 0.5.1.

### Changed

- All GitHub Actions workflows now reference concrete runner labels
  (no `*-latest` aliases, no removed `macos-13`):
  - `ubuntu-latest` → `ubuntu-24.04`
  - `windows-latest` → `windows-2022`
  - `macos-13` → `macos-15-intel`
- Removed `continue-on-error` for Windows ARM64 — both Windows x64
  and ARM64 MSVC builds are now genuinely green.
- Removed the `Threads::Threads` public link dependency from the
  library target. The library uses `pthread_self()` (libc) for PRNG
  seed on POSIX and `GetCurrentThreadId()` on Windows — no pthread
  link needed. The generated `otlp-c-config.cmake` no longer drags
  in a Threads `find_dependency`.
- `property-exporter` test runs separately with `continue-on-error`
  in CI. The test has a known thread-scheduling race in its
  in-process echo server; library code is sound (25/26 tests pass
  deterministically across all 7 platforms).
- `cmake-integration` job's Windows entry marked `continue-on-error`
  pending investigation of an install-path mismatch in the consumer
  test.

### CI matrix coverage

| Platform | Runner | Status |
|---|---|---|
| Linux x64 gcc | ubuntu-24.04 | pass |
| Linux x64 clang | ubuntu-24.04 | pass |
| Linux ARM64 gcc | ubuntu-24.04-arm | pass |
| macOS Intel | macos-15-intel | pass |
| macOS ARM64 | macos-14 | pass |
| Windows x64 MSVC | windows-2022 | pass |
| Windows ARM64 MSVC | windows-11-arm | pass |
| Alpine x64 (musl) | alpine:3.21 container | pass |
| Alpine arm64 (musl) | alpine:3.21 container | pass |
| FreeBSD 14.2 | vmactions/freebsd-vm | pass |
| CMake find_package (Linux/macOS) | — | pass |
| CMake find_package (Windows) | — | gated (path issue) |

## [0.5.1] - 2026-08-07

Bug-fix release. Restores Windows MSVC support broken by the
preview VS 18 toolchain's `<stdatomic.h>` rejecting the
`_HAS_C11_ATOMICS=1` macro override.

### Added

- `src/atomic_compat.h`: thin abstraction over the small subset
  of C11 `<stdatomic.h>` the library uses (atomic_load / store /
  compare_exchange / fetch_add on uint64_t and int). Pass-through
  to `<stdatomic.h>` on GCC/Clang; MSVC intrinsics
  (`_InterlockedCompareExchange64`, `_InterlockedExchange64`,
  `_InterlockedExchangeAdd`) on Windows.

### Fixed

- **Windows MSVC build**: was failing with `fatal error C1189:
  "C atomic support is not enabled"` because VS 2022's vcruntime
  checks for actual compiler atomics support, not just the macro
  override. The `atomic_compat.h` shim removes the `<stdatomic.h>`
  dependency entirely on MSVC.
- `nanosleep` was POSIX-only; replaced with `Sleep(1)` on Windows
  in the exporter's tick loop.
- `mpsc_queue_size` had a const-correctness issue with the new
  atomic wrapper; cast away const (the load is conceptually
  read-only).
- Removed `continue-on-error` for Windows x64 MSVC in CI — the
  build is now genuinely green.

### Changed

- `mpsc_queue.c`, `tracer.c`, `exporter.c` refactored to use
  `otlp_atomic_*` wrappers instead of `<stdatomic.h>` directly.
- `_Atomic uint64_t` / `_Atomic int` field types replaced with
  `otlp_atomic_u64` / `otlp_atomic_int`.
- `atomic_compat.h` is the single source of truth for atomic
  operations. Adding new atomic types is a one-function-per-type
  extension (no switch, no #ifdefs at call sites). OCP.

### CI

- MSVC dev environment pinned to VS 2022 stable
  (`ilammy/msvc-dev-cmd@v1` `vsversion: 2022`) in both `ci.yml`
  and `release.yml`. Avoids the VS 18 preview toolchain entirely.
- `test_property_mpsc` gated to POSIX (uses pthreads directly;
  the queue itself is portable).

### Known limitations

- Windows ARM64 still `continue-on-error` — runner is slow, build
  succeeds when it gets a runner.
- `property-exporter` test still flakes on Linux under ctest
  parallel load. Pre-existing, documented.

## [0.5.0] - 2026-08-07

The "actually complete the TODOs" release. Closes TODOs 20, 21, 22,
23, 24, 27, and 42 with full implementations (the prior "Complete"
markers were based on stubs). Adds two architectural refactors that
bring the metrics and logs encoders into the same model-driven shape
as traces.

### Added — Signals

- **OTLP metrics signal** (TODO 20, `include/otlp-c/metric.h`):
  counter / gauge / histogram types with `record()`, time setters,
  and attribute setters. Wire encoder produces
  `ExportMetricsServiceRequest` bytes via the model-driven schema
  tables.
- **OTLP logs signal** (TODO 21, `include/otlp-c/log.h`):
  `LogRecord` with 24-level severity enum, body, trace_id/span_id
  correlation, attribute setters. Wire encoder produces
  `ExportLogsServiceRequest` bytes.
- **Span events + links** (TODO 22): `otlp_span_add_event`,
  `otlp_span_add_link`, `otlp_span_set_trace_state` are no longer
  stubs. The encoder emits them at OTLP Span fields 3/11/13.
  Fixed-cap storage: 64 events, 64 links per span.
- **SpanContext propagation** (TODO 23, `include/otlp-c/context.h`):
  value-type `otlp_context_t` + callback-based carrier abstraction
  (`otlp_carrier_set_fn` / `otlp_carrier_get_fn`) +
  `otlp_context_from_span` / `_inject` / `_extract`. Transport-
  agnostic by design.
- **Sampler interface** (TODO 24, `include/otlp-c/sampler.h`):
  pluggable vtable (`otlp_sampler_t`) with three built-ins:
  `always_on`, `always_off`, `trace_id_ratio_based`. Tracer
  consults the sampler at `start_span`; NOT_RECORD returns NULL.

### Added — Performance

- **HTTP keep-alive + connection reuse** (TODO 27): exporter
  caches one TCP connection between batches, eliminating DNS lookup
  + TCP handshake cost on steady-state emission. New
  `otlp_http_request_start_with_socket` and
  `otlp_http_request_detach_socket` APIs. Parser detects explicit
  `Connection: close` and disables reuse.
- **Slab allocator** (TODO 42, `include/otlp-c/slab.h`): standalone
  fixed-slot-size memory pool with malloc fallback. Drop-in for
  any malloc/free pair. Stats exposed for observability.

### Changed — Architectural

- **Schema-driven metrics/logs encoders** (DRY/OCP): all three
  signal encoders now reference field numbers via named-enum
  indices into `otlp_schema.h` tables. Eliminates ~30 local
  `#define`s. Adding a new message type is one schema entry, not
  a new `#define`.
- **Table-driven metric-kind dispatch** (OCP): the encoder's
  per-metric-type switch is replaced with a `metric_kind_specs[]`
  dispatch table. Adding a new metric type (e.g.,
  ExponentialHistogram) is one function + one table entry.
- **Shared encoder helpers** (DRY): `otlp_encode_any_value`,
  `otlp_emit_resource`, `otlp_emit_instrumentation_scope` extracted
  from `otlp_messages.c` as non-static. All three signal encoders
  compose them — no copy-paste of the resource envelope.
- **`otlp_span_is_sampled()` now public** (was internal-only).
  Symmetric with `otlp_span_set_sampled()`.

### Added — Tests

- 6 metrics encoder properties (counter/gauge/histogram field
  numbers, value round-trip, attribute round-trip).
- 6 logs encoder properties (severity present/omitted, body
  round-trip, trace correlation, attributes).
- 6 events/links/context properties (events round-trip, links
  round-trip, trace_state field, clone copies extras, context
  inject/extract, context rejects malformed).
- 7 sampler properties (always_on/off, ratio extremes, deterministic,
  distribution, default sampler).
- 6 slab allocator properties (roundtrip, slot reuse, oversize
  fallback, overflow fallback, free routing, stats consistency).
- 3 keepalive properties (disabled on explicit close, eligible by
  default, reuse round-trip).

Total: 16 property tests, all passing.

### Known limitations

- Tail sampling deferred (the API surface for "decide at end_time"
  doesn't fit the caller-tick exporter model cleanly).
- Slab allocator is standalone; integration into `otlp_malloc` /
  `otlp_free` is a follow-up (needs benchmarking to confirm net
  win on the realistic emit path).
- Multi-connection HTTP pool deferred (currently 1 cached socket
  per exporter).
- Windows MSVC `<stdatomic.h>` still broken in VS preview; CI uses
  continue-on-error. Tracked in TODO phase-20.
- The `property-exporter` test is intermittently flaky under ctest
  parallel load (timing-sensitive). Passes in isolation. Tracked.

### Compatibility

- Linux x86_64, macOS arm64, macOS x86_64, FreeBSD 14.2.
- Windows x64 / ARM64: builds, MSVC atomics workaround in place,
  CI is continue-on-error pending MSVC team fix.
- C11 compiler required.
- Static and shared library configurations both supported.

Within the 0.x line, the API may break between minor versions.

## [0.3.0] - 2026-08-05

### Added

### Changed

### Fixed


## [0.2.0] - 2026-08-05

### Added

### Changed

### Fixed


Within the 0.x line, minor versions may break the public API.
Breaking changes are explicitly flagged with **BREAKING**.

## [0.1.0] — 2026-08-05

Initial alpha release. The library emits OTLP/HTTP trace spans
from pure C99 with zero non-libc dependencies.

### Added — Core

- **Protobuf wire encoder** (`src/protobuf_encode.{h,c}`): varint,
  fixed64, fixed32, length-delimited primitives; typed field
  helpers with protobuf3 default-omission semantics. Bounded
  growth buffer (SIZE_MAX/2 cap). [Phase 1]
- **OTLP message encoders** (`src/otlp_messages.{h,c}`):
  just-in-time encoder from `otlp_span_t*` to wire bytes for the
  full `ExportTraceServiceRequest` envelope (Resource, Scope,
  Span, Status, KeyValue, AnyValue). [Phase 2]
- **Span builder** (`src/span.c`): opaque `otlp_span_t` with all
  14 public setters; fixed-cap (128) attribute array; deep-clone
  for exporter queueing. [Phase 4]
- **Tracer** (`src/tracer.c`): xorshift64s PRNG with C11 atomic
  CAS for lock-free multi-threaded `start_span`; W3C-style random
  trace/span IDs with all-zero rejection. [Phase 4]
- **Platform layer** (`src/platform.{c,h}`, `platform_unix.c`,
  `platform_win.c`): cross-platform clocks + non-blocking socket
  primitives (connect_nb / finish_connect / write_nb / read_nb).
  No thread / mutex / condvar abstractions. [Phase 3]
- **HTTP/1.1 client** (`src/http_client.{h,c}`): URL parser
  (`http://` only; rejects `https://`, malformed ports) +
  non-blocking POST state machine (`start`/`step`/`state`/`fd`/
  `events`) driven by the caller. [Phase 3]
- **Lock-free MPSC queue** (`src/mpsc_queue.{h,c}`): Vyukov
  bounded ring with per-slot sequence numbers on C11 atomics.
  Power-of-2 capacity, default 4096. [Phase 5]
- **Exporter** (`src/exporter.c`, `src/exporter_otel.{h,c}`):
  caller-tick model with `otlp_exporter_tick(exp, max_wait_ms)`
  and `otlp_exporter_poll_fds()`. Deep-copy `emit()` (caller may
  free immediately). Atomic stats counters. Exponential backoff
  on 429/5xx/network with attempt cap. [Phase 5]

### Added — Tests

- 7 property tests: varint (round-trip, size, extremes),
  encoder fields, span lifecycle + ID uniqueness, attribute
  round-trip, OTLP message structure, URL parser.
- 3 unit tests: smoke (API surface), HTTP echo (state machine
  against in-process server), exporter echo (end-to-end emit +
  tick + flush against in-process server).
- 1 integration test: real otelcol + Jaeger topology via
  `docker compose`; emits 100 tagged spans, queries Jaeger API,
  asserts visibility. Gated by `OTLP_C_RUN_INTEGRATION=1`.

### Added — Infrastructure

- Property-test harness (`tests/property/{prng.h,property_harness.h}`)
  with env-var seed/iteration overrides for reproducible failures.
- Test-helper echo server (`tests/test_helper_echo.{h,c}`) for
  HTTP layer and exporter tests.
- CMake build (3.20+), Ninja recommended. Options:
  `OTLP_C_BUILD_TESTS`, `OTLP_C_BUILD_EXAMPLES`,
  `OTLP_C_ENABLE_ASAN`, `OTLP_C_ENABLE_UBSAN`, `BUILD_SHARED_LIBS`.
- Multi-platform CI matrix: Linux x86_64, macOS arm64, macOS
  x86_64, Windows x64. vcpkg manifest mode (zero required deps).
- `.clang-format` (Mozilla-based) + `ci/checkpatch.sh`.

### Added — Documentation

- `README.md` — pitch, comparison table, quick-start with tick
  pattern, sidecar deployment note.
- `docs/quickstart.md` — install + first program + running a
  local otelcol + Jaeger.
- `docs/deployment.md` — sidecar TLS topology (Kubernetes
  DaemonSet, sidecar container, systemd VM); why no TLS in the
  library; caller-tick embedding patterns by host environment.
- `docs/integration-test.md` — how to run the integration test.
- `examples/minimal.c` — working example emitting one span.

### Architectural commitments

- **Pure C99 baseline, C11 for `<stdatomic.h>`** (CLAUDE.md
  allows C11 atomics). No other C11 features in use.
- **Zero non-libc dependencies.** No protobuf-c, no libcurl, no
  OpenSSL, no C++ runtime.
- **No library threads, no locks.** All cross-thread data flow
  via atomics + MPSC queue. Embeddable in kernel modules,
  firmware, language VMs, libc-preload contexts.
- **Caller-driven I/O.** The library never calls `pthread_create`
  or `_beginthreadex`. `otlp_exporter_tick()` advances the
  in-flight HTTP state machine from a caller-owned thread.
- **Apache 2.0** throughout, for the eventual CNCF donation path.

### Known limitations

- Metrics (`/v1/metrics`) and Logs (`/v1/logs`) signals deferred
  to a later minor.
- TLS is **not** in the library. Production deployments use an
  otelcol sidecar for TLS termination. Direct-to-cloud HTTPS is
  not a 0.1.x use case.
- Single in-flight HTTP request per exporter. Multiple in-flight
  requests (pipelining) is post-1.0.
- DNS resolution is blocking `getaddrinfo` (one-shot per request,
  cached per exporter for the process lifetime). Non-blocking
  DNS is post-1.0.
- `ArrayValue` and `KeyValueList` AnyValue variants deferred.
- Events, links, and `trace_state` fields are not yet emitted.
- Attribute count is capped at 128 per span (compile-time).
- No JSON encoding (protobuf only).

### Compatibility

- Linux x86_64, macOS arm64, macOS x86_64, Windows x64.
- C11 compiler required (GCC 4.9+, Clang 3.8+, MSVC 2019+).
- Static and shared library configurations both supported.

### API surface

The 0.1.0 public API is in `include/otlp-c/`:

- `otlp.h` — umbrella header + `otlp_version()`.
- `version.h` — version macros.
- `status.h` — `otlp_status_t` error codes.
- `visibility.h` — `OTLP_C_EXPORT` symbol annotation.
- `span.h` — `otlp_span_t` + 14 setters + lifecycle.
- `tracer.h` — `otlp_tracer_t` + ID-generating span factory.
- `exporter.h` — `otlp_exporter_t` + `emit`/`tick`/`flush`/
  `shutdown`/`poll_fds`/`get_stats`.

Within the 0.x line, the API may break between minor versions;
changes will be documented in this file.
