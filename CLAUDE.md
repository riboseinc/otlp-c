# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What otlp-c is

`otlp-c` is a pure-C99 client library for the OpenTelemetry Protocol (OTLP/HTTP). It emits all three OTLP signals — traces, metrics, and logs — over HTTP/1.1 to an OpenTelemetry collector.

The hard constraint that defines this project: **zero non-libc dependencies**. No C++ runtime. No vendored protobuf. No TLS library (defer TLS to a local `otelcol` sidecar). No async runtime. The library must build with a C99 compiler and link cleanly into any C application, including kernel modules, embedded firmware, language runtimes, and libc-preloaded tracers.

## The non-negotiable invariants

These are not stylistic preferences; they are load-bearing. Breaking any of them defeats the project's reason for existing.

1. **Pure C99.** No C++ files. No `extern "C"` wrappers around C++ implementations. If you need atomic operations, use C11 `<stdatomic.h>` or platform intrinsics — never C++ atomics.
2. **Zero non-libc runtime dependencies.** No protobuf, no gRPC, no libcurl, no OpenSSL, no zlib. The encoder is hand-rolled for the ~6 OTLP message types we need. See `docs/otlp-spec.md` for the schema.
3. **Apache 2.0 only.** Every line committed must be Apache-2.0 compatible. Don't introduce BSD-only or GPL code. This matters for the eventual CNCF donation path.
4. **No telemetry from otlp-c itself.** The library does not phone home. No version checks. No usage reporting.
5. **Public API stability within a major version.** Once 1.0.0 ships, no breaking API changes within the 1.x line. New features = new functions or new opt-in structs. See `docs/roadmap.md` for the version policy.
6. **Clean default builds.** `cmake -B build && cmake --build build` must succeed and link a working library. All implementations are real — no stubs, no `OTLP_ERR_NOT_IMPLEMENTED`.

## Build

CMake 3.20+, Ninja recommended.

```sh
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Useful CMake options:

- `OTLP_C_BUILD_TESTS` (default OFF) — build the test binaries in `tests/`.
- `OTLP_C_BUILD_EXAMPLES` (default OFF) — build the demo programs in `examples/`.
- `OTLP_C_ENABLE_ASAN` (default OFF) — AddressSanitizer.
- `OTLP_C_ENABLE_UBSAN` (default OFF) — UndefinedBehaviorSanitizer.
- `BUILD_SHARED_LIBS` (default OFF) — build as shared library instead of static.

vcpkg manifest mode is supported; the manifest currently declares no required dependencies (by design).

## Running tests

CTest drives the test pyramid. Labels: `unit`, `property`, `integration`, `smoke`.

```sh
ctest --test-dir build                 # all
ctest --test-dir build -L unit         # unit only
ctest --test-dir build -L property     # property-based
ctest --test-dir build -L integration  # needs Docker for otelcol
```

Property tests take an env var override for the iteration count: `OTLP_C_PROPERTY_ITERS=100000 ctest ...`. Reproduce a specific failure with `OTLP_C_PROPERTY_SEED=<seed>`.

## Code style

- **`.clang-format`** is Mozilla-based. Run `clang-format -i` on changed files before committing.
- **checkpatch** (Linux kernel style with project-specific ignores) runs in CI. See `ci/checkpatch.sh`.
- **Common gotchas**:
  - Block comments: `*/` on its own line for multi-line comments.
  - Blank line after declarations before statements.
  - No `do { ... } while (0)` macros with control flow inside (use static inline functions instead).
  - No multi-statement macros with side effects.

## Architecture

See [docs/architecture.md](docs/architecture.md) for the full layered view. Summary:

```
caller code  ───►  public API (include/otlp-c/)  ───►  implementation (src/)
                                                                │
                                                                ▼
                                              ┌─────────────────┴──────────────────┐
                                              │                                    │
                                     protobuf encoder                  HTTP/1.1 client
                                     (src/protobuf_encode.c)            (src/http_client.c)
                                              │                                    │
                                              └─────────────────┬──────────────────┘
                                                                │
                                                                ▼
                                                          OTLP/HTTP wire
                                                                │
                                                                ▼
                                                          otelcol (external)
```

Module responsibilities are MECE: each file owns exactly one concern. The protobuf encoder doesn't know about HTTP. The HTTP client doesn't know about OTLP. The exporter batches spans and calls both.

## Don'ts

- **Don't add a third-party dependency.** If you reach for libcurl or protobuf-c, you're solving the wrong problem. Hand-roll the surface you need in `src/`.
- **Don't use C++.** C99 only. C11 is acceptable if `<stdatomic.h>` is needed; document why.
- **Don't change the public API without bumping the major version.** (Within the 0.x line, the API is allowed to break; document changes in CHANGELOG.)
- **Don't add telemetry-from-the-library.** The library emits OTLP about the user's code, never about itself.
- **Don't reach into another module's static state.** Use accessor functions; declare opaque types in headers.
- **Don't hand-edit vendored .proto definitions.** Hand-roll the message structs in `src/otlp_messages.h` and keep them in sync with the spec at `docs/otlp-spec.md`.

## Key files to know

| Path | Purpose |
|---|---|
| `CMakeLists.txt` | All build switches, feature probes |
| `vcpkg.json` | Dependency manifest (empty by design — zero deps) |
| `include/otlp-c/otlp.h` | Umbrella public header |
| `include/otlp-c/exporter.h` | Exporter — emit/tick/flush for all 3 signals + diagnostics |
| `include/otlp-c/span.h` | Span type — events, links, attributes, sampling |
| `include/otlp-c/value.h` | `otlp_value_t` — public scalar AnyValue + `otlp_kv_t` (composite attributes) |
| `include/otlp-c/metric.h` | Metric types (counter/gauge/histogram/exp-histogram) |
| `include/otlp-c/log.h` | Log records with trace correlation |
| `include/otlp-c/sampler.h` | Sampler vtable + 3 built-ins |
| `include/otlp-c/context.h` | W3C Trace Context + Baggage propagation |
| `include/otlp-c/slab.h` | Slab allocator + global integration |
| `src/otlp_schema.h` | Schema tables — single source of truth for field numbers |
| `src/protobuf_decode.{h,c}` | Bounds-checked wire-format reader (PartialSuccess decode) |
| `src/otlp_messages.c` | Traces encoder + shared helpers (any_value, resource) |
| `src/otlp_metrics_encoder.c` | Metrics encoder (table-driven dispatch) |
| `src/otlp_logs_encoder.c` | Logs encoder |
| `src/exporter.c` | Exporter lifecycle — 3 MPSC queues, batch, retry, flush, diagnostics |
| `src/http_client.c` | HTTP/1.1 non-blocking state machine + keep-alive + timeouts |
| `src/atomic_compat.h` | Atomic abstraction (MSVC intrinsics fallback) |
| `tests/golden/` | Reference-serialized golden vectors + generator |
| `tests/test_util.h` | check_ok/check_true — always-evaluated, always-enforced test checks |
| `tests/property/` | Property-based tests (QuickCheck-style, deterministic) |
| `tests/integration/` | End-to-end against a local otelcol |
| `bench/bench_emit.c` | Emit pipeline throughput benchmark |
| `docs/architecture.md` | Layered design (21+ modules) |
| `docs/roadmap.md` | Status and version plan |
| `.github/workflows/ci.yml` | Multi-platform CI + sanitizers (ASAN/UBSAN/TSAN) |

## The OTLP protocol

OTLP/HTTP is well-specified by [opentelemetry.io](https://opentelemetry.io/docs/specs/otlp/). The full reference (with field numbers) is in [docs/otlp-spec.md](docs/otlp-spec.md).

What you need to know day-to-day:

- **Endpoint**: `POST http://<host>:4318/v1/traces` for traces. Same pattern for metrics (`/v1/metrics`) and logs (`/v1/logs`).
- **Body**: a Protobuf-encoded `ExportTraceServiceRequest` message.
- **Content-Type**: `application/x-protobuf`.
- **Response**: 200 OK on success; 4xx / 5xx with a structured error message on failure.
- **Schema**: lives in [opentelemetry-proto](https://github.com/open-telemetry/opentelemetry-proto), Apache 2.0. We hand-roll the message structs (see `src/otlp_messages.h`).

## For the implementing agent

All phases are complete (v0.5.97). The library implements:
- Full protobuf wire encoder with schema-driven field tables
  (every OTLP message has its own field-spec entry — v0.5.48/49/61
  fixed 10 field-number/wire-type bugs found by cross-checking
  against upstream opentelemetry-proto)
- All three OTLP signals (traces, metrics, logs) with encoders
- Span/metric/log lifecycle with events, links, attributes
- **Async emission for ALL three signals** via MPSC queue + caller-tick.
  Each signal has both clone (`emit` / `emit_metric` / `emit_log`) and
  move (`emit_move` / `emit_metric_move` / `emit_log_move`) variants.
  tick() uses a table-driven `struct signal_path` descriptor for DRY
  dispatch across all three signals (v0.5.28–v0.5.30). The emit,
  record_outcome, start_post, and free-drain paths are all
  descriptor-driven (v0.5.43–v0.5.46).
- W3C Trace Context propagation (traceparent + tracestate) +
  **W3C Baggage** (v0.5.22); ID setters reject all-zero (v0.5.54);
  context extract rejects CRLF in tracestate/baggage (v0.5.53)
- Resource attributes: **on the one value model** (v0.5.92:
  `{key, otlp_value_t}` with all AnyValue types; map semantics at
  create time via the set engine) + configurable aggregation
  temporality + is_monotonic (v0.5.26)
- Sampler interface (always_on / always_off / trace_id_ratio_based)
- Lock-free MPSC queue + caller-tick exporter (no library threads)
- Non-blocking HTTP/1.1 client with keep-alive +
  **connect/read timeout enforcement** (v0.5.25) +
  **no-Content-Length handling per RFC 7230** (v0.5.47) +
  **header-injection hardening at all vectors** (v0.5.52/53: CWE-93)
- **Diagnostic callback** for production observability (v0.5.23):
  `otlp_exporter_set_logger()` fires at 7 events (queue full, retry,
  drop, success, etc.)
- Slab allocator (standalone + global integration)
- ExponentialHistogram with configurable buckets
- Null-transport mode for deterministic testing
- Configurable flush timeout + compile-time span cap overrides
- Per-signal stats (emitted/sent/dropped for spans, metrics, logs) +
  **accounting invariant holds under OOM** (v0.5.59)
- **Fail-injecting allocator test infrastructure** (v0.5.56/57):
  every multi-alloc init path is probed at each OOM offset; catches
  partial-init cleanup leaks and accounting violations automatically
- **Integer-overflow defense at all allocation sites** (v0.5.62/63:
  CWE-190): every `count * sizeof(...)` has an explicit check
- **Hardened HTTP client** (v0.5.80/84): chunked-response decoding
  (RFC 7230 §4.1, in-place), line-aligned header matching,
  TE+CL/duplicate-CL smuggling rejection, version-aware
  keep-alive, and an I/O inactivity deadline covering CONNECTING,
  SENDING, and READING (send-side slowloris closed)
- **W3C-spec-exact context propagation** (v0.5.81): traceparent
  version rules (0xff invalid; version 00 exactly 4 fields;
  future-version forward compat), printable-ASCII-only propagated
  tracestate/baggage, big-endian ratio-sampler prefix
- **Retry with full jitter** (v0.5.83): uniform
  [0, min(initial<<(attempt−1), max)] via a tick-thread PRNG;
  shift-count-clamped exponent (no UB at large max_retries);
  **Retry-After honored** (v0.5.95): delay = max(jitter, server
  floor) clamped by backoff_max_ms on 429/503/5xx
- **Structured diagnostics events** (v0.5.100):
  `otlp_exporter_set_event_logger()` delivers every diagnostic as
  an `otlp_event_t` (QUEUE_FULL / BATCH_SENT / RETRY_ARMED /
  ITEMS_DROPPED / PARTIAL_SUCCESS / SYNC_FLUSH_FAILED + signal id
  + counts + drop reason); the legacy string messages are derived
  from the same event by one formatter
- **PartialSuccess decoding** (v0.5.96): 200-OK bodies carry
  server-reported data loss (rejected counts + message) —
  surfaced via WARN diagnostic + per-signal rejected_* stats.
  First library wire-format DECODER (`src/protobuf_decode.c`,
  schema-table-driven; bounds-checked, fails closed)
- **Wire-schema verified against opentelemetry-proto** (v0.5.97):
  field numbers audited against upstream (fixed HDP min/max 10/11
  → 11/12); `tests/unit/test_unit_wire_numbers.c` pins encoded
  bytes AND schema tables against upstream literals — never the
  schema's own numbers (self-referential checks can't catch
  schema drift)
- **Enforced test checks** (v0.5.98/99): `tests/test_util.h`
  `check_ok()`/`check_true()` take the expression as an argument
  AND abort on failure in every configuration — 536 value-asserts
  that were elided in Release CI now verify AND fail in both
  configurations (v0.5.98's first cut evaluated but didn't
  enforce under NDEBUG; fixed in v0.5.99)
- **UTF-8 boundary validation** (v0.5.103): every wire string
  (attribute keys/values, names, service_name, composites) is
  validated at the setter — proto3 strings must be valid UTF-8
  and otelcol rejects whole requests otherwise; invalid input
  returns OTLP_ERR_UTF8, bytes values are exempt
- **Golden vectors** (v0.5.101): `unit-golden` compares whole
  payloads against the reference opentelemetry-proto
  serialization (generated by `tests/golden/generate.py`, manual
  regeneration) as canonical field trees — zigzag, packing,
  presence, and values validated end-to-end, not just field
  numbers. New signals/fixtures: extend the generator + C
  fixtures in lockstep and regenerate.
- **Schema pinned to upstream** (v0.5.99): every table in
  `src/otlp_schema.h` is field-number-checked against
  opentelemetry-proto literals by `unit-wire-numbers` in every
  build; a new schema table MUST get a pin entry there
- **Arena-aware slab realloc** (v0.5.85): any slot size is safe —
  growing arena pointers are moved, never libc-realloc'd

When extending the library:
- **All six attribute surfaces** (span/event/link/metric/log/
  resource) take all seven AnyValue types: string/bool/int64/
  double/bytes via per-type setters (or the `otlp_value_t` field
  for resource attrs); ARRAY/KVLIST via the composite setters
  taking `otlp_value_t` arrays.
- **New attribute type**: add enum value + encoder function +
  `attr_encoders[]` table entry in `otlp_messages.c`. OCP.
- **New metric type**: add enum + schema + encoder + dispatch
  table entry in `otlp_metrics_encoder.c`. OCP.
- **New sampler**: implement the `otlp_sampler_t` vtable.
- **Schema changes**: edit `src/otlp_schema.h` tables (single
  source of truth for all field numbers).

Run `ctest --test-dir build -L property` after every change; the
property tests catch regressions in the encoder immediately.

Test-writing rules (paid-for lessons):
- **Never put side effects in `assert()`** — CI's plain jobs build
  Release/NDEBUG, which elides the expression entirely
  (v0.5.82–84: pthread_create, bind/listen/getsockname, and
  parse_url all vanished under Release). Explicit rc checks.
- **Use `tests/test_util.h` `check_ok()`/`check_true()` for every
  result checked once** (v0.5.98): passing the expression as an
  argument means it ALWAYS executes — plain `assert(st == OTLP_OK)`
  is elided under Release, making the check (and, when it was the
  only use of the variable, a zero-warning build) silently vanish.
  536 such asserts were converted in v0.5.98; before that, Release
  CI verified almost nothing in the unit tests.
- Verify in BOTH Debug and Release locally
  (`cmake -B build-rel -DCMAKE_BUILD_TYPE=Release`); Release also
  spins drive loops ~100× faster — bound timeout-waiting loops by
  wall clock, not iteration count.
- macOS ASAN does not enable leak detection by default: run
  `ASAN_OPTIONS=detect_leaks=1` (ignore framework-only leaks in
  property-url-parse; Linux CI is the authoritative gate).

## Conventions

- **Opaque types**: every public type is `typedef struct foo foo;` in the header, with the struct definition in the .c file. No public struct members.
- **Error codes**: every public function returns `otlp_status_t`. `OTLP_OK` (0) is success; negative values are errors. See `include/otlp-c/status.h`.
- **Ownership**: functions that return heap-allocated pointers have `_create` / `_free` pairs. The caller owns the pointer between them. Document ownership in the docstring.
- **Threads**: the library is lock-free. Cross-thread data flow uses
  atomics (via `atomic_compat.h`) + MPSC queue. The exporter is
  caller-tick driven — the caller drives I/O via `otlp_exporter_tick()`
  from a thread it controls. Span construction is single-threaded by
  design — each thread builds its own span.
- **Three signals, one pipeline**: traces, metrics, and logs all flow
  through the same MPSC + tick + retry pipeline (v0.5.28). The exporter
  has three queues (span/metric/log); tick() drains all three by
  priority and POSTs one signal at a time to /v1/traces, /v1/metrics,
  or /v1/logs. `emit()` / `emit_move()` (spans), `emit_metric_move()`,
  and `emit_log_move()` are all safe to call from any thread.
- **Diagnostics**: two views of one model. Every diagnostic the
  exporter emits is an `otlp_event_t` (code, signal, counts,
  level, drop reason) — `otlp_exporter_set_event_logger()`
  delivers the struct; `otlp_exporter_set_logger()` receives the
  message DERIVED from it by one formatter (`format_event`), so
  the views cannot diverge. Install either/both/neither; default
  none, zero overhead (NULL checks). Thread-safe by contract.
- **Stats**: `otlp_exporter_get_stats()` returns per-signal counters
  (emitted/sent/dropped for spans, metrics, and logs) plus global
  HTTP-level counters (2xx/4xx/5xx/network_err).
- **Memory**: use the platform's `malloc`/`free`. Custom allocators are a P1 feature; defer.
- **Attributes are a map** (v0.5.73): setting an existing key
  replaces its value (last write wins, type may change) — the OTLP
  data model requires unique keys. All five attribute-bearing
  surfaces (span, event, link, metric, log) share one
  grow-on-demand vector model (`struct otlp_attr_vec`) owned by
  `internal_util`'s `otlp_attr_vec_{reserve,copy,free}`; storage
  cost tracks actual use (empty objects pay pointers, not
  cap-sized arrays). Composite values (ARRAY/KVLIST) come from the
  public `otlp_value_t` / `otlp_kv_t` via the `*_set_attribute_
  {array,kvlist}` setters. One set-attribute engine
  (`otlp_attr_vec_set` / `_set_array` / `_set_kvlist` in
  internal_util) owns the whole flow — every public setter is a
  thin typed wrapper, so guard/OOM semantics exist in exactly one
  place.

## CI

`.github/workflows/build.yml` runs:

- Linux x86_64, macOS arm64, macOS x86_64, Windows x64 (matrix).
- Build + unit tests + property tests.
- vcpkg manifest mode.

`.github/workflows/checkpatch.yml` runs checkpatch on every PR.

PRs require green CI before merge. Rebase-merge is the canonical merge style.

## Versioning

Semantic versioning (semver.org). Within the 0.x line, the API may break between minor versions — document changes in `CHANGELOG.md`. At 1.0.0, the API freezes for the 1.x line.

The version is defined in `include/otlp-c/version.h` and bumped in lockstep with `CMakeLists.txt` and `vcpkg.json`.

## See also

- [README.md](README.md) — the pitch.
- [docs/otlp-spec.md](docs/otlp-spec.md) — the protocol reference.
- [docs/architecture.md](docs/architecture.md) — the design.
- [docs/roadmap.md](docs/roadmap.md) — the plan.
- [SECURITY.md](SECURITY.md) — disclosure handling.
- [CONTRIBUTING.md](CONTRIBUTING.md) — contribution workflow.
