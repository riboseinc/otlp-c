# Roadmap

Phase-by-phase implementation plan. Each phase has acceptance
criteria. Don't skip ahead — later phases depend on earlier ones
being solid.

## Status legend

- **Done** — landed on main.
- **In progress** — actively being worked.
- **Ready** — well-understood; can start.
- **Design** — needs an ADR before code.
- **Future** — after 1.0.

## Phases

| # | Phase | Status | Depends on |
|---|---|---|---|
| 0 | Bootstrap | Done | — |
| 1 | Protobuf wire encoder | Done | 0 |
| 2 | OTLP message struct definitions | Done | 0 |
| 3 | HTTP/1.1 client | Done | 0 |
| 4 | Span builder | Done | 1, 2 |
| 5 | Exporter (batch + flush) | Done | 1, 2, 3, 4 |
| 6 | Integration test against otelcol | Done | 5 |
| 7 | Documentation polish | Done | 6 |
| 8 | Tag 0.1.0 | Done | 1–7 |
| 9 | DRY extraction + caller-tick | Done | 5 |
| 10 | Span move semantics | Done | 5 |
| 11 | Model-driven encoder | Done | 2 |
| 12 | Concurrency tests | Done | 5 |
| 13 | Deferred stubs spec | Done | 4 |
| 14 | Arena allocator | Removed | — |
| 15 | Doxygen + cookbook | Done | 7 |
| 16 | CMake presets + CPack | Done | — |
| 17 | Benchmark suite | Done | 5 |
| 18 | Code coverage | Done | — |
| 19 | Security hardening | Done | — |
| 20 | Windows MSVC fix | Deferred (MSVC team) | — |
| 21 | vcpkg registry | Done | 16 |
| 22 | SBOM + signing | Done | 21 |

## v0.4 (current) — Multi-signal

| # | Item | Status | PR |
|---|---|---|---|
| 20 | OTLP metrics signal (counter / gauge / histogram) | Done | #23 |
| 21 | OTLP logs signal (severity / body / trace correlation) | Done | #23 |

## v0.5 — Architectural improvements + feature completion

| # | Item | Status | PR |
|---|---|---|---|
| — | Schema-driven metrics/logs encoders (DRY/OCP) | Done | #24 |
| — | Table-driven metric-kind dispatch (no switch) | Done | #24 |
| 22 | Span events + links wire encoding | Done | #24 |
| 23 | SpanContext propagation (inject/extract) | Done | #24 |
| 24 | Head sampler interface (always_on/off/ratio) | Done | #24 |
| 27 | HTTP keep-alive + connection reuse | Done | #25 |
| 42 | Slab allocator (standalone) | Done | #25 |

### v0.5.18–v0.5.36 — correctness, architecture, API completion

| Version | Item | PR |
|---|---|---|
| 0.5.18 | TSAN race fix in test infrastructure (atomic echo_server state) | #49 |
| 0.5.19 | Policy-docs staleness audit (SECURITY.md, README, etc.) | #48 |
| 0.5.20 | Resource attributes (string-only, additive to opts) | #50 |
| 0.5.21 | Configurable flush_timeout_ms + multithread example + null_transport backoff fix | #51 |
| 0.5.22 | W3C Baggage + traceparent_format_raw DRY extraction | #52 |
| 0.5.23 | Diagnostic callback + **critical MPSC data-loss fix** (queue never enforced capacity) | #53 |
| 0.5.24 | Typed Resource attributes (int64/double/bool) | #54 |
| 0.5.25 | HTTP connect/read timeout enforcement (was dead config) | #55 |
| 0.5.26 | Configurable metric temporality + is_monotonic (was hardcoded) | #56 |
| 0.5.27 | Header audit + emit benchmark + compile-time cap overrides | #57 |
| 0.5.28 | **Async metric/log batching** (the #1 architectural gap) | #58 |
| 0.5.29 | Documentation accuracy audit (CLAUDE.md + architecture + README) | #59 |
| 0.5.30 | tick() DRY refactor (table-driven struct signal_path dispatch) | #60 |
| 0.5.31 | emit_metric / emit_log clone variants (API symmetry) | #61 |
| 0.5.32 | Decouple internal_util.h + span_clone DRY + cookbook patterns | #62 |
| 0.5.33 | **Fix: span_clone dropped event/link attributes** + flush stats gap | #63 |
| 0.5.34 | Multi-signal concurrency stress test (8 threads × 3 signals, TSAN-clean) | #64 |
| 0.5.35 | Fix: null_transport backoff-retry double-processing + metric/log retry tests | #65 |
| 0.5.36 | CLAUDE.md + README accuracy audit | #66 |

**Key metrics (v0.5.36):** 76 TODOs complete, 34 tests, 7 correctness
bugs found and fixed, all sanitizers green, zero warnings.

### v0.5.37–v0.5.63 — deep audit arc (wire format, security, memory, overflow)

| Version | Item | PR |
|---|---|---|
| 0.5.37 | Roadmap update (docs catch-up to v0.5.36) | #67 |
| 0.5.38 | Pre-sized encode buffers (perf: ~10 fewer growth cycles per batch) | #68 |
| 0.5.39 | `otlp_pb_buf_init` return checks + batch-encode benchmark | #69 |
| 0.5.40 | **Fix: metric/log body leak on encode fail** + keepalive reuse + MECE refactor (exporter_otel build helpers) | #70 |
| 0.5.41 | **Fix: move-emit leaked donated item on shutdown-return** | #71 |
| 0.5.42 | Clone-variant emit shutdown-before-alloc symmetry | #72 |
| 0.5.43 | Table-driven emit pipeline (descriptor + shared helpers) | #73 |
| 0.5.44 | Table-driven `record_outcome` (descriptor + signal-agnostic helpers) | #74 |
| 0.5.45 | Table-driven `try_start_post` (descriptor + build-request wrappers) | #75 |
| 0.5.46 | Table-driven exporter free-drain + span clone-shutdown test | #76 |
| 0.5.47 | **Fix: `otlp_attribute_copy_all` fail-path leak** + HTTP no-Content-Length parser fix (RFC 7230) | #77 |
| 0.5.48 | **Fix: 4 OTLP schema field-number bugs** (Event name/time swap, Status.code=1→3, NDP attrs=1→7, HDP attrs/min/max) | #78 |
| 0.5.49 | **Fix: 2 ExpHistogram wire types** (zero_count VARINT→FIXED64, bucket_counts fixed64→varint) | #79 |
| 0.5.50 | **Fix: LogRecord asymmetric trace correlation** emitted zero-fill trace_id (W3C violation) | #80 |
| 0.5.51 | **Fix: slab double-free UB** (free on arena pointer) + sampler endpoint precision | #81 |
| 0.5.52 | **Fix: HTTP header injection (CWE-93)** — URL parser + user_agent CR/LF | #82 |
| 0.5.53 | **Fix: context propagation CRLF injection (CWE-93)** — tracestate/baggage | #83 |
| 0.5.54 | **Fix: 5 ID setters accepted all-zero (W3C §3.1.1/§3.1.2)** | #84 |
| 0.5.55 | **Fix: resource_attributes used `malloc` (fail path iterated garbage)** | #85 |
| 0.5.56 | **Fail-injecting allocator test + fix: mpsc_queue cleanup leak in exporter_create fail path** | #86 |
| 0.5.57 | Extended OOM tests to all init paths (140 new iterations) | #87 |
| 0.5.58 | **Fix: flush return-status omitted queue-size checks** (silent data loss) | #88 |
| 0.5.59 | **Fix: flush_metric/flush_log accounting broke under OOM** (`emitted != sent + dropped_err`) | #89 |
| 0.5.60 | Span/log docstring accuracy (post-audit catch-up) | #90 |
| 0.5.61 | ExponentialHistogram schema entry (DRY/MECE — was reusing Histogram's) | #91 |
| 0.5.62 | **Fix: integer overflow in metric allocations (CWE-190→CWE-787)** | #92 |
| 0.5.63 | **Fix: integer overflow sweep — all remaining allocation sites** (dup_str, mpsc_queue, resource_attrs, batch_size clamp) | #93 |
| 0.5.64 | Roadmap + CLAUDE.md catch-up (27 releases) | #94 |
| 0.5.65 | DNS behavior documentation accuracy | #95 |
| 0.5.66 | Integration test validates events + status; CI runs it end-to-end | #96 |
| 0.5.67 | Integration test covers all three signals; sync-flush retry + diagnostics | #97 |
| 0.5.68 | Span struct 15.7× smaller (139KB → 8.8KB; lazy event/link attrs) — emit 20× faster | #99 |
| 0.5.69 | Metric/log structs 19×/52× smaller (lazy attrs); log emit ~5× faster | #100 |
| 0.5.70 | One owner for the lazy attribute-list model (`otlp_attr_list_*` in internal_util; DRY across 4 sites) | #101 |
| 0.5.71 | Attribute setter type parity (metric bool/bytes; log double/bool/bytes) | #102 |
| 0.5.72 | Typed event/link attribute setters (int/double/bool/bytes) | #103 |
| 0.5.73 | **Attributes are a map: last-write-wins upsert** (duplicate keys could reach the wire) | #104 |
| 0.5.74 | **ARRAY/KVLIST attributes end-to-end + fix: composite AnyValue frames were malformed** (missing LEN prefix) | #105 |
| 0.5.75 | Grow-on-demand attribute vectors everywhere; span struct 5.8KB; 1-attr span ~4× faster + **fix: NULL-span setter guards** | #106 |
| 0.5.76 | Span events/links grow on demand; **span struct 176 bytes** (789× smaller than v0.5.67); emit ~150 ns/span | #107 |
| 0.5.77 | Documentation catch-up for the attribute-model arc | #108 |
| 0.5.78 | **Resource attributes are a map** (duplicate opts keys / service.name collisions emitted non-compliant wire data) | #109 |
| 0.5.79 | One set-attribute engine (`otlp_attr_vec_set/_set_array/_set_kvlist`); all 29 setters are thin wrappers (−145 lines, semantics byte-identical) | #110 |
| 0.5.80 | **Chunked HTTP responses** (RFC 7230 §4.1 in-place decoder) + framing hardening (line-aligned headers, TE+CL smuggling rejection, version-aware keep-alive) | #111 |
| 0.5.81 | W3C traceparent version rules (ff invalid; v00 exact) + control-byte rejection in propagated tracestate/baggage | #112 |
| 0.5.82 | MPSC/shutdown audit: queue verified correct by ordering-pair trace; shutdown concurrency contract documented + protocol stress test | #113 |
| 0.5.83 | **Backoff full jitter implemented** (docs had promised it since v0.1) + shift-UB fix for large max_retries + 429 → http_4xx bucket | #114 |
| 0.5.84 | **SENDING-phase I/O inactivity deadline** (send-side slowloris) + big-endian ratio-sampler prefix | #115 |
| 0.5.85 | **Slab allocator arena-aware realloc** (libc realloc on arena pointers was UB — aborts under macOS libmalloc) | #116 |
| 0.5.86 | Encode-path audit: SBO 64 → 192 (span envelopes escape was the only heap hit); ~10% on attribute batches, byte-identical | #117 |
| 0.5.87 | Documentation catch-up for the audit arc (CLAUDE.md test rules distilled from the three NDEBUG/leak lessons) | #118 |
| 0.5.88 | **Fix: `otlp_version()` returned "0.5.68" for 19 releases** (hand-maintained string literal; now derived from the macros); examples refreshed | #119 |
| 0.5.89 | Fuzz coverage: raw/mutated HTTP responses through the real state machine (chunked decoder); context-extract fuzz | #120 |
| 0.5.90 | **Fix: `OTLP_C_ENABLE_COVERAGE` never instrumented anything** (flags after the target); exporter_otel 30% → 75% via real-HTTP metric/log tests | #121 |
| 0.5.91 | Coverage lap 2: platform + tracer error paths (DNS failure, connection refused, NULL guards); platform 76→82%, tracer 81→91% | #122 |
| 0.5.92 | **Resource attributes on the one value model** (breaking: `{key, otlp_value_t}`; all AnyValue types; encoder type-switch deleted) + OOM realloc-accounting fix | #123 |
| 0.5.93 | (bookkeeping release) | #124 |
| 0.5.94 | Public-header API coherence: duplicate `metric.h` declarations removed; uniform return-code docs on metric/log headers | #125 |
| 0.5.95 | **Retry-After honored** (RFC 7231 §7.1.3): delay = max(jitter, server floor) clamped by backoff_max; wire-tested; Release-vacuous test asserts fixed | #126 |
| 0.5.96 | **PartialSuccess surfaced** (OTLP server-side data-loss reporting): new `protobuf_decode` module, WARN diagnostic + per-signal rejected_* stats; lost v0.5.92 property re-registered | #127 |
| 0.5.97 | **Fix: histogram min/max at wrong wire fields** (10/11 → upstream 11/12 — collectors dropped min, doubled max); EHDP flags wire type corrected (dormant); upstream-literal wire test breaks the self-referential test circle | #128 |
| 0.5.98 | **Release builds verified almost nothing**: 536 assert-only checks elided under NDEBUG converted to always-evaluated `check_ok`/`check_true` (tests/test_util.h); zero Release warnings restored; exporter-echo's silently-timing-out join (worker outliving main) fixed | #129 |
| 0.5.99 | **Fix: v0.5.98 checks didn't ENFORCE in Release** (false check didn't fail — helpers now abort in every config, mutation-tested); all 31 schema tables pinned against upstream literals; otlp-spec.md gains the missing metrics/logs field reference | #130 |
| 0.5.100 | **Structured diagnostics events**: `otlp_exporter_set_event_logger()` delivers every diagnostic as an `otlp_event_t` (code/signal/counts/drop reason); string messages now DERIVED from the event by one formatter (14 printf sites removed) | #131 |
| 0.5.101 | **Golden vectors**: payloads built by the reference opentelemetry-proto serialization and compared as canonical field trees (zigzag/packing/presence validated, not just field numbers); decoder gains fixed32/64 readers | #132 |
| 0.5.102 | Hygiene: concurrency-stress echo worker stop + checked joins (last `(void)` joins in the tree); minimal example demos the event callback | #133 |

**Key metrics (v0.5.102):** 142 TODOs complete, 45 tests, **48+
distinct bugs found and fixed**, all sanitizers green, zero
warnings, verified in Debug AND Release configurations, every
library file at 82%+ region coverage. `sizeof(otlp_span)`:
138,880 → **176 bytes**; emit pipeline ~30,000 → **~150 ns/span**
(~6.6M spans/s, null transport); encode ~990 ns/span at 5 attrs
(SBO 192).

**Unification arc (v0.5.68–v0.5.92):** every attribute surface —
span, event, link, metric, log record, resource — now shares one
public value type (`otlp_value_t`), one storage model
(grow-on-demand `otlp_attr_vec`), one set engine
(`otlp_attr_vec_set/_set_array/_set_kvlist`), and one encoder
dispatch (`otlp_encode_any_value`).

**Audit arc (v0.5.78–v0.5.86):** every subsystem passed through a
dedicated audit — resource attrs, setter engine, HTTP response
parser, W3C context, MPSC/shutdown, retry/backoff, request
timeouts + sampler, slab, encoders. Findings per pass: 2–5 real
bugs each (UB, spec violations, smuggling vectors, doc-vs-code
lies). Two test-discipline lessons banked: macOS ASAN needs
`detect_leaks=1` explicitly; side-effecting calls must never sit
in `assert()` (Release/NDEBUG elides them).

**Attribute-model arc (v0.5.68–v0.5.76):** all five
attribute-bearing surfaces (span, event, link, metric, log) share
one grow-on-demand vector model with map (upsert) semantics and
the full AnyValue type set (string/bool/int64/double/bytes/array/
kvlist), wire-verified by property tests. Storage cost tracks
actual use: empty objects pay pointers, not cap-sized arrays.

**Bug-class coverage in the v0.5.47–v0.5.63 arc:**
- Wire format: 10 bugs (schema field numbers, wire types).
- W3C spec: 3 bugs (trace correlation, ID validation).
- Memory safety: 4 bugs (partial-init cleanup, slab double-free).
- Security: 4 bugs (CWE-93 header injection at 3 vectors).
- Accounting: 3 bugs (return-status, counter invariants under OOM).
- Integer overflow: 8+ sites (CWE-190).
- Test infrastructure: fail-injecting allocator across 8 init/flush paths.
- Schema completeness: every OTLP message has its own field-spec table.
- Documentation: public API docstrings aligned with post-audit behavior.

## Deferred (post-1.0)

These are documented in TODO.complete/ with full specs but not on
the v1.0 critical path. Reasons vary (see each file).

| # | Item | Reason |
|---|---|---|
| 25 | TLS support | Sidecar model — otelcol terminates TLS. v1.x WONTFIX. |
| 26 | OTLP gRPC transport | Sidecar model — HTTP/1.1 is sufficient. v1.x WONTFIX. |

## Out of scope (this repo)

| # | Item | Reason |
|---|---|---|
| 11 | FreeBSD CI | Best-effort: in CI matrix via vmactions/freebsd-vm (continue-on-error). |
| 29 | Retrace otel sink | Separate project. |
| 30 | Language bindings | Each binding is its own repo. |
| 31 | Distro packaging | Linux distros are slow; separate effort. |

## Phase 0 — Bootstrap (this commit)

Goal: a project skeleton that compiles, links a stub library, runs
empty tests, and documents everything the next agent needs.

Files:
- [x] `README.md`, `CLAUDE.md`, `LICENSE`, `SECURITY.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`
- [x] `CMakeLists.txt`, `vcpkg.json`, `.clang-format`, `.gitignore`
- [x] `include/otlp-c/*.h` — public API
- [x] `src/*.c` — stub implementations returning `OTLP_ERR_NOT_IMPLEMENTED`
- [x] `tests/property/` — property harness skeleton + one property
- [x] `examples/minimal.c` — minimal usage example
- [x] `docs/otlp-spec.md` — protocol reference
- [x] `docs/architecture.md` — design
- [x] `docs/roadmap.md` — this file
- [x] `.github/workflows/build.yml` — multi-platform CI

Acceptance criteria:
- [x] `cmake -B build -G Ninja && cmake --build build` succeeds.
- [x] The stub library links into `examples/minimal.c`.
- [x] `ctest --test-dir build -L property` runs the seed property.
- [x] CI is green on Linux + macOS + Windows.

## Phase 1 — Protobuf wire encoder

Goal: implement `src/protobuf_encode.c` against the public API
promised in `src/protobuf_encode.h`.

Functions to implement:
- `otlp_pb_varint(buf, n)` — encode a varint.
- `otlp_pb_fixed64(buf, val)` — encode a fixed64.
- `otlp_pb_fixed32(buf, val)` — encode a fixed32.
- `otlp_pb_bytes(buf, len, data)` — length-delimited bytes.
- `otlp_pb_string(buf, str)` — length-delimited string.
- `otlp_pb_tag(buf, field_num, wire_type)` — key encoding.
- `otlp_pb_skip_field(buf, ...)` — skip past a field.

Acceptance criteria:
- [ ] All varint test vectors from [the Protobuf docs](https://protobuf.dev/programming-guides/encoding/) round-trip correctly.
- [ ] Property P-VARINT-ROUNDTRIP: any `uint64_t` survives encode → decode.
- [ ] Property P-VARINT-SIZE: the encoded size matches `ceil(log2(n) / 7)`.
- [ ] No crashes on edge cases (0, UINT64_MAX).

## Phase 2 — OTLP message struct definitions

Goal: hand-roll the C struct definitions matching the OTLP .proto.

File: `src/otlp_messages.h`.

Structs (one per .proto message):
- `otlp_resource_spans_t`
- `otlp_resource_t`
- `otlp_scope_spans_t`
- `otlp_instrumentation_scope_t`
- `otlp_span_t` (internal — the public type is opaque)
- `otlp_status_t` (the message type, not the error-code enum)
- `otlp_key_value_t`
- `otlp_any_value_t`
- `otlp_event_t`
- `otlp_link_t`

Plus the encoder entry point:
- `otlp_encode_export_trace_service_request(buf, request)` — emits a full request.

Acceptance criteria:
- [ ] Each struct's field numbers match `docs/otlp-spec.md` exactly.
- [ ] Property P-ENCODE-NEVER-CORRUPT: encoding any valid request
  produces a byte sequence that the official opentelemetry-cpp
  decoder can decode without error.
- [ ] Property P-ENCODE-EMPTY: encoding an empty request produces
  a zero-length body.

## Phase 3 — HTTP/1.1 client

Goal: `src/http_client.c` can POST a byte buffer to a URL.

Public API in `include/otlp-c/http_client.h`:
- `otlp_http_request_t` — struct holding URL, body, headers.
- `otlp_http_response_t` — struct holding status, body.
- `otlp_http_post(const request *, response **)` — POST, return status.

Implementation:
- Raw socket. POSIX and Win32 paths in `src/platform.c`.
- No TLS. Document that production users should run an `otelcol`
  on localhost for TLS termination.
- Per-request connect (P1: connection pool).

Acceptance criteria:
- [ ] POST to a local HTTP echo server returns the body unchanged.
- [ ] Property P-HTTP-NEVER-HANG: any URL/host/port combination
  either succeeds, fails with a known error, or times out within
  the configured window.
- [ ] Property P-HTTP-NO-CRASH: any malformed URL is rejected with
  `OTLP_ERR_INVALID_ARGUMENT`, never crashes.

## Phase 4 — Span builder

Goal: `src/span.c` and `src/tracer.c` implement the public span
API from `include/otlp-c/span.h` and `tracer.h`.

Features:
- Span name, start/end time, attributes (string, int64, double,
  bool, bytes).
- Status (UNSET / OK / ERROR + description).
- SpanKind (INTERNAL default).
- Parent linking (tracer-internal context).
- Trace ID + span ID generation (random; seedable for tests).

Acceptance criteria:
- [ ] All 12 setters in the public API work.
- [ ] Property P-SPAN-ATTRIBUTES: setting an attribute and reading
  it back returns the same value.
- [ ] Span IDs are 8 random bytes; trace IDs are 16 random bytes.
- [ ] No leaks (verify with ASAN build).

## Phase 5 — Exporter (batch + flush)

Goal: `src/exporter.c` and `src/exporter_otel.c` implement the
exporter API from `include/otlp-c/exporter.h`.

Features:
- Batching by size (default 512 spans) and time (default 100ms).
- Background flush thread.
- Retry with exponential backoff on 429/5xx/network errors.
- Drop with counter on overflow.

Architecture: see [docs/architecture.md](architecture.md).

Acceptance criteria:
- [ ] A 1000-span emit produces 2 POST requests (or 1 if flushed
  within 100ms).
- [x] Failed POST triggers exponential backoff with full jitter (v0.5.83).
- [ ] Property P-EXPORT-NEVER-CORRUPT: no batch ever reaches the
  collector with malformed protobuf body.
- [ ] Property P-EXPORT-NO-LEAK: exporter shutdown frees all
  resources.

## Phase 6 — Integration test

Goal: `tests/integration/test_end_to_end.c` runs against a local
`otelcol` Docker container.

Setup:
- `docker-compose.yml` in the repo runs `otelcol` + Jaeger.
- Test emits 100 spans, waits 2 seconds, queries Jaeger's API,
  asserts the spans are visible.

Acceptance criteria:
- [ ] `docker compose up` + `ctest -L integration` produces visible
  spans in Jaeger.
- [ ] Test passes on Linux + macOS (Windows deferred — Docker
  Desktop works but CI is slow).

## Phase 7 — Documentation polish

Goal: everything a new user needs is in `docs/`.

- [ ] Update README with end-to-end working example.
- [ ] Add `docs/quickstart.md` — 5-minute getting started.
- [ ] Add `docs/api-reference.md` — generated from headers (doxygen?).
- [ ] Add `docs/cookbook.md` — common patterns.

## Phase 8 — Tag 0.1.0

Goal: cut the first release.

- [ ] Version bump to 0.1.0 in `CMakeLists.txt`, `vcpkg.json`,
  `include/otlp-c/version.h`.
- [ ] `CHANGELOG.md` updated.
- [ ] Tag `v0.1.0`. Release workflow produces binary artifacts.
- [ ] Announcement blog post.

## Future (after 1.0)

- **Metrics signal** (POST /v1/metrics).
- **Logs signal** (POST /v1/logs).
- **OTLP/gRPC** transport (lower latency; needs HTTP/2 client).
- **TLS** via OS-native (Secure Transport on macOS, schannel on
  Windows, OpenSSL optionally on Linux).
- **Sampling** at the SDK level (head-based and tail-based).
- **Custom allocators** (caller supplies malloc/free).
- **Language bindings**: Python (cffi), Rust (#repr(C)), Go (cgo).
- **CNCF donation** once the API stabilizes and adoption warrants it.

## How to pick what to work on

1. **Phases 1–5 are sequential.** Don't parallelize.
2. **Phase 6 can start in parallel with 7** once 5 is done.
3. **Open a draft PR early** for each phase. Mark WIP.
4. **Property tests are mandatory** before merge. If a phase's
   acceptance criteria include property tests, they must land with
   the phase.

## When the plan changes

This roadmap is a plan, not a contract. When reality diverges:

- **Update this file** to reflect the new plan.
- **Bump the affected phase's status** (`In progress` → `Design`,
  `Ready` → `Future`, etc.).
- **Note the divergence in the commit message** so future you
  understands why.
