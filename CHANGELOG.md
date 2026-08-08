# Changelog

All notable changes to `otlp-c` are documented here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the project adheres to [Semantic Versioning](https://semver.org/).

## [0.5.18] - 2026-08-08

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
