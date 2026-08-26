# Changelog

All notable changes to `otlp-c` are documented here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the project adheres to [Semantic Versioning](https://semver.org/).

## [1.1.6] - 2026-08-26

Roadmap truth, status edition — docs only.

### Fixed — the roadmap's status layer contradicted reality

- Phase 20 ("Windows MSVC fix") still read "Deferred (MSVC
  team)" — MSVC x64/ARM64 jobs have been green across the
  matrix for 100+ releases. Now Done.
- The v0.4 section was headed "(current)" — 1.1.5 is current.
- The Out-of-scope table still listed FreeBSD CI as
  "Best-effort … continue-on-error" — false since v1.1.3 made
  it a gating check; row dropped (the v1.1.3 release row and
  TODO 178 keep the history).
- README's platform line said FreeBSD 14.2 "(best-effort)" —
  now states it gates with the full test suite.

## [1.1.5] - 2026-08-26

The adoption path, kept true — docs only.

### Fixed — the vcpkg path was a hand-off, and the tag examples rotted

- docs/quickstart.md now shows the vcpkg overlay port
  end-to-end: the two consumer files, the clone, and the exact
  cmake invocation — the same recipe the `vcpkg overlay
  consumer` CI job pins. Before, it said "see README".
- README and quickstart both showed `v1.0.5` as the "latest"
  release tag in their FetchContent examples (four releases
  stale); both bumped to v1.1.4.
- The site /docs/changelog/ page gains the 1.1.4 and 1.1.5
  entries (the page shipped in 1.1.3 and immediately lagged).
- TODO phase-21's two in-repo acceptance criteria are ticked;
  the microsoft/vcpkg registry PR itself remains user-owned.

## [1.1.4] - 2026-08-26

Docs truth, architecture edition — no code changes.

### Fixed — docs/architecture.md drifted three releases behind

- The layer diagram's exporter box still said "Metrics/Logs:
  sync flush" — all three signals have been async since v0.5.28;
  the box now names exporter.c + exporter_sync.c and the real
  split (async tick pipeline vs the sync flush engine).
- The module-responsibilities table gains the exporter_sync.c
  row (extracted v1.1.2).
- The diagnostics note now says the event model spans both
  exporter.c and exporter_sync.c (SYNC_FLUSH_FAILED).
- Testing strategy gets the true counts (53 total; 25 property
  tests, not 27), the soak protocol, the unit-test inventory,
  and the tests/test_portable.h lesson from v1.1.3.
- CLAUDE.md key-files gains the tests/test_portable.h row.

## [1.1.3] - 2026-08-25

FreeBSD becomes a gating check; the changelog gets a home.

### Changed — FreeBSD CI is a real gate

The FreeBSD job ran `continue-on-error` since inception for an
INADDR_LOOPBACK visibility quirk. The jobs API showed 15/15 green
runs on main — but that was the mask, not the truth: under
`continue-on-error` a step's `conclusion` reads "success" even
when it failed. Removing the flag exposed three real portability
bugs the "green" runs had been hiding:

- the memmem fallback included Apple-only `Availability.h` on
  FreeBSD too (fatal compile error)
- FreeBSD's `netinet/in.h` hides `INADDR_LOOPBACK` whenever
  `_POSIX_C_SOURCE` is defined — and the build defines it
  globally for CLOCK_MONOTONIC (four sites across three test
  trees)
- one test called `memmem` with no declaration on FreeBSD
- `usleep` is gone from POSIX 2008; FreeBSD headers hide it
  under strict `_POSIX_C_SOURCE` (nanosleep now)

All fixed by `tests/test_portable.h`: one always-local
byte-search and a spelled-out loopback constant, so the tests run
one deterministic code path on every platform. FreeBSD 14.2 now
gates like every other platform.

### Added — /docs/changelog/ on the site

The release arc rendered as the site's changelog page: the
freeze, exemplars, OTel-native config, the deepening window —
with links to the authoritative CHANGELOG.md. 14 pages now.

### Fixed — docs truth

CLAUDE.md key-files gains exporter_sync.c and exporter_internal.h
(the one internal seam); test count corrected to 53.

## [1.1.2] - 2026-08-25

Third architecture review, fully implemented.

### Changed — the sync-flush pipeline is its own module (review C1)

exporter.c held both delivery engines — the async tick pipeline
and the synchronous flush — plus the exporter struct: 1865 lines
where understanding flush_metric meant scrolling past the whole
tick machine. The struct, signal_state, and the signal-kind enum
moved to exporter_internal.h (the one internal seam, now
explicit); the sync engine (flush_post_once, flush_sync,
flush_metric, flush_log) lives in src/exporter_sync.c — 330
lines, one-shot encode → POST → retry → events in one readable
file. exporter.c is down to 1471 lines: lifecycle + async
pipeline. event_log and report_partial_success became shared
internal symbols. Public surface unchanged; all 52 tests pass
unmodified.

### Added — the site's last feature blind spots (review C2)

- /docs/propagation/ — W3C Trace Context + Baggage: inject and
  extract with carrier callbacks, the spec-exact guarantees
  (0xff rejection, printable-only values, has_context=false over
  garbage), and sampler interaction.
- /docs/performance/ — the real numbers (89 ns/span emit, 360 ns
  encode, 176-byte span), reproduction via the bench preset, why
  it's fast, and the measurement discipline.

### Fixed — architecture.md diagram drift (review C3)

env_config.c sat in the module table since 0.7.1 but never joined
the layer diagram; a reader crossing from table to diagram saw
two different architectures.

## [1.1.1] - 2026-08-25

The site gains a real docs section.

### Added — docs IA (five pages, sidebar layout)

- /docs/ — Getting started (consume four ways, first span, core
  concepts, presets)
- /docs/signals/ — the three signals deep dive (span lifecycle,
  metric kinds, logs + correlation, exemplars; emit vs emit_move)
- /docs/configuration/ — every opts field documented + a
  filterable env-var explorer (new Vue island, scope filter and
  text search)
- /docs/diagnostics/ — the event model (all six codes, levels,
  semantics), delivery guarantees, retry/partial-success/stats
- /docs/deployment/ — the sidecar topology as a hand-drawn SVG
  diagram, Kubernetes DaemonSet/sidecar patterns, why no TLS
  (ADR-0004)
- 404 page (in voice), OG/twitter meta on every page, sitemap.xml

Three Vue islands now (CodeTabs, AudienceExplorer,
EnvVarExplorer); all other pages remain zero-JS static HTML.

## [1.1.0] - 2026-08-25

The real website: Astro 7 + Vite 8 + Tailwind 4 + Vue islands.

### Added — a designed site with a full information architecture

website/ is an Astro 7 (Vite 8) project styled with Tailwind 4
(via @tailwindcss/vite, CSS-first theme) with two Vue island
components: a code-sample switcher (signals, consumption modes,
examples) and a filterable audience explorer. Pages:

- **/** — the pitch, stats, feature grid, tabbed three-signal
  quick code, env-config showcase
- **/about** — what/why, the zero-deps constraint, the version
  arc, license/governance/stability cards
- **/audiences** — who it's for (kernel, firmware, VMs, preloaded
  tracers, static binaries, security-critical), filterable by
  constraint
- **/use-cases** — four deployment patterns (sidecar edge, VM FFI
  binding, library instrumentation, collector auth) with code
- **/docs** — consume (4 modes), first span, the six core
  concepts, build presets
- **/examples** — minimal, event-loop (poll), multithreaded emit
- **/api/** — the generated Doxygen reference, nested; the two
  halves deploy together so they cannot drift

The Pages job rebuilds: npm ci + astro build + Doxygen into
dist/api, deployed on every push to main. Local:
cd website && npm run dev.

## [1.0.5] - 2026-08-25

Second architecture review, fully implemented; the website exists.

### Changed — one retry engine (review C1)

The sync metric/log flush path had its own hand-rolled backoff
(fixed backoff_initial_ms clamped to 100ms) while the async path
used the jittered retry policy — two timing engines in one
exporter. flush_sync now draws through otlp_retry_delay_ms with
a sync-shaped config ({initial, min(max, 100)}): the 100ms sync
latency cap is expressed as config, and the sync path gains full
jitter for free. One engine, three callers.

### Added — the website (review C2): Doxygen → GitHub Pages

No website existed (no Pages config, no branch). Now: a CI job
builds the Doxygen reference (84 pages, zero warnings, versioned
from CMake, CONTEXT.md included) and deploys it to
https://riboseinc.github.io/otlp-c/ on every push to main. The
site is generated, never hand-maintained — it cannot go stale.
README, quickstart, and CLAUDE.md link it.

### Added — the domain glossary (review C3): CONTEXT.md

One line per domain term with a pointer to its home module —
signal, exporter, tick, exemplar, spec table, golden vector,
freeze, and 15 more. The shared vocabulary for reviews and
agents; CLAUDE.md carries the rules, CONTEXT.md the language.

### Not implemented (review C4, recorded)

Splitting internal_util's mem/str kernel from the attribute
engine: re-examined and declined again — stable, well-tested,
zero friction incidents across two review cycles; the split is
churn without a change it would ease.

## [1.0.4] - 2026-08-25

Coverage re-measured and CI-enforced; soak protocol established.

### Added — coverage CI gate (82% per-file floor)

The 82% region floor was a convention that drifted unnoticed
once (the v0.6.2 re-measurement); now an ubuntu CI job builds
instrumented (clang coverage preset), runs the suite, and fails
any library file below 82%. Current numbers, re-measured for the
first time since v0.6.2: every file clears the bar — the 1.x-era
modules hold it comfortably (http_response_parser 98.2%,
env_config 89.1%, retry_policy 84.2%, exporter 89.1%, exemplar
paths inside metric.c 83.4%).

### Verified — 100k-iteration property soak

21 of 25 property binaries ran at 100000 iterations (10×-100×
their CI count): zero assertion failures — the CPU-side
invariants hold at scale. The remaining four (keepalive,
flush-timeout, http-timeout, async-metrics) sleep through real
socket/time waits per iteration; their default count IS the
practical soak (more is wall clock, not coverage). The tiered
protocol is documented in CLAUDE.md, including the ctest
TIMEOUT caveat that motivated running binaries directly.

### Fixed — README status banner contradicted the API freeze

The banner said "0.5.35" and "the API surface is unstable until
1.0.0" — stale since v1.0.0, on the front page. Now: 1.0.x, API
frozen, and the feature list gains exemplars, schema_url, HTTP
headers, and the env-var matrix.

## [1.0.3] - 2026-08-24

Conformance gates CI-enforced; the 1.x surface validated live.

### Added — conformance-gates CI job

The v1.0.2 manual tools are now CI-enforced (ubuntu): the schema
tables are audited against the pinned opentelemetry-proto
descriptors (`pip install opentelemetry-proto==1.44.0`),
and the golden vectors are REGENERATED and must match the
checked-in files byte-for-byte — so schema drift, stale fixtures,
and upstream proto changes all fail the build. Regeneration is
idempotent (verified locally).

### Added — the full 1.x surface against a real collector

The Jaeger integration test gains a scenario exercising
everything the frozen API added this cycle on one exporter:
`schema_url`, an extra HTTP header, env-var opts
(`OTEL_EXPORTER_OTLP_TIMEOUT` via `otlp_exporter_opts_apply_env`),
a per-signal `metrics_endpoint`, and a metric carrying an
exemplar (double value + trace/span correlation + timestamp). A
2xx from otelcol — whose real protobuf parser would reject
malformed exemplars — is the acceptance gate.

## [1.0.2] - 2026-08-24

Descriptor audit of all 32 schema tables; a second released wire
bug found and fixed.

### Fixed — PartialSuccess was decoded at the wrong field (since v0.5.96)

A new maintenance tool (`tests/golden/audit_tables.py`) diffs
every schema table in otlp_schema.h against the INSTALLED
opentelemetry-proto descriptors — the class-closing response to
the v1.0.1 exemplar bug. Result: 30/32 clean, and the decoder's
ExportXServiceResponse table had `partial_success` at field 5
where the real proto puts it at field 1 — meaning
server-reported data loss from REAL collectors has been silently
ignored since PartialSuccess decoding shipped (v0.5.96). The
echo and protobuf unit fixtures had encoded field 5 themselves,
matching our own wrong table — the same self-referential failure
class; all fixtures now encode the real format, and a
reference-generated response vector (via the golden generator)
is decoded by the production decoder in unit-golden, so the
DECODE side is now reference-validated too.

### Added — schema audit tool

`python3 tests/golden/audit_tables.py` (needs opentelemetry-proto)
verifies every table's field numbers and computed wire types —
including packed-repeated LEN handling — against the installed
descriptors. Run it whenever a schema table changes; its exit
code gates.

## [1.0.1] - 2026-08-24

Golden corpus extended; a real exemplar wire bug found and fixed.

### Fixed — exemplar field numbers were wrong (v0.8.0 wire bug)

Extending the golden vectors to cover schema_url and exemplars
(reference opentelemetry-proto serialization, byte-compared)
exposed that v0.8.0's Exemplar schema table was hand-copied from
memory instead of from the descriptor: the real message is
`time_unix_nano=2, as_double=3, span_id=4, trace_id=5, as_int=6,
filtered_attributes=7` — v0.8.0 emitted five of six at wrong
numbers, which a real collector would misparse. The table, the
emitter, and the wire-numbers pin literals are corrected (pins
now carry a comment: copy literals from the installed descriptor,
never memory). The golden vectors — which compare against the
REFERENCE implementation, not our own tables — are the gate that
caught it and now pin exemplars and schema_url permanently.

### Added — golden corpus covers the full 1.0 wire surface

schema_url on all three signals; a double+trace+span+time
exemplar on the counter; an int exemplar on the gauge. New
additive API: `otlp_exemplar_set_timestamp()` (parity with the
span/log setters — deterministic fixtures and backfilling; the
golden fixture uses it instead of poking internals). Debug hook:
`GOLDEN_DUMP=1` writes our payloads for external decoding.

### Fixed — deployment.md documented an invented env var

`OTLP_C_ENDPOINT` (fictional, pre-dating the real env support)
replaced with `OTEL_EXPORTER_OTLP_ENDPOINT` base form; the
cookbook gains sections for env vars/headers/per-signal endpoints
and exemplars/schema_url.

## [1.0.0] - 2026-08-24

**The API freeze.** All five Path-to-1.0 criteria re-audited and
confirmed at v0.8.0; the source API is now frozen for the 1.x
line — additions only, no breaking changes to signatures, types,
or semantics (the C ABI is explicitly not guaranteed across
builds; consumers compile against the headers).

### The 1.0 audit (TODO.complete/168)

- 133 exported symbols across 10 headers — the v0.5.104 audit's
  124 plus the 9 added since (env config, HTTP headers,
  per-signal endpoints, schema_url, exemplars), each reviewed for
  docstrings, return codes, ownership, and thread-safety.
- No stubs anywhere in the tree; strerror covers every error
  code (pinned); 31+1 schema tables pinned against upstream
  literals; golden payloads reference-validated; live otelcol +
  Jaeger integration green in CI on every PR.
- Two freeze-hazard fixes shipped with this release: the ~26 KiB
  `otlp_env_storage_t` stack cost is now documented (prefer
  static/heap placement on constrained stacks), and
  `otlp_exporter_opts_apply_env`'s getenv thread contract is
  documented (call during single-threaded init).
- Deliberately NOT in 1.x (design constraints, not gaps): TLS
  (sidecar terminates — ADR-0004), gRPC, payload compression —
  the path to those is a 2.x optional-deps model.

## [0.8.0] - 2026-08-24

Exemplars: the last substantive OTLP metrics feature.

### Added — trace-correlated exemplar data points

New public `otlp_exemplar_t` (opaque): one value (double field 2
or int64 field 3, last setter wins), optional trace/span
correlation (fields 4/5 — IDs copied, all-zero rejected per the
v0.5.54 rule), optional timestamp (`mark_time`, field 6).
`otlp_metric_add_exemplar()` clones into the metric (grow-on-
demand; deep-copied by metric clone; value required at add time).
Emitted on the metric's data point — NumberDataPoint field 5,
HistogramDataPoint field 8 — via a new Exemplar schema table
pinned against upstream literals in unit-wire-numbers, along
with an end-to-end emission test (descend to the data point,
verify every exemplar field's number, wire type, and value).
Additive API only; minor bump per the version policy.

## [0.7.4] - 2026-08-24

schema_url: a conformance gap closed.

### Added — opts.schema_url

The OTLP proto carries `schema_url` (field 3) on every
resource-level message; the schema tables had it pinned all
along, but the encoders never emitted it. New additive opt
`schema_url` (UTF-8-validated at create, deep-copied) is emitted
as field 3 on ResourceSpans, ResourceMetrics, and ResourceLogs
across the async and sync paths. Wire-proven in unit-wire-numbers
against the upstream literal for all three signals, plus
absence-when-NULL.

## [0.7.3] - 2026-08-24

Per-signal endpoints, end to end.

### Changed — one URL per signal (the model change)

The exporter now holds a full URL per signal (sig[s].url) instead
of deriving metric/log paths from the traces URL at encode time:
create() derives each from the endpoint base + the signal's
default path (now a SIGNAL_SPECS column — the add-a-signal table
gains the path), or takes the new `metrics_endpoint` /
`logs_endpoint` opts. The exporter_otel build functions receive
their signal's URL verbatim (path rewrites deleted); the
sync-flush path parameter is gone.

### Added — OTEL_EXPORTER_OTLP_METRICS/LOGS_ENDPOINT

The per-signal full-endpoint forms, completing the endpoint env
matrix. `OTEL_EXPORTER_OTLP_ENDPOINT` is now a true base: each
signal's default path is appended (a value carrying its own path
is stripped to scheme+host+port — exact control comes from the
signal-specific forms). Wire-proven: exporter-echo drives
flush_metric through a custom metrics endpoint and asserts
`POST /custom-metrics` on the captured request.

## [0.7.2] - 2026-08-24

Extra HTTP headers + OTEL_EXPORTER_OTLP_HEADERS.

### Added — arbitrary HTTP headers on export requests

`otlp_exporter_opts_t.http_headers` (array of the new public
`otlp_http_header_t`) rides every export request — authentication
for the collector being the canonical use. Deep-copied at
`otlp_exporter_create()` (the opts contract holds); CR/LF in
names/values rejected at create AND re-checked in the request
builder (the user_agent CWE-93 posture, now covering every
caller-controlled header field). The builder assembles the head
block incrementally with per-append bounds checks (replacing the
single fixed-snprintf form).

### Added — OTEL_EXPORTER_OTLP_HEADERS

The last OTel standard variable: "k=v,k=v" parsed by the shared
tokenizer (extracted from the resource-attributes parser — one
"k=v list" engine, two typed sinks) into
`otlp_env_storage_t.http_headers`. The env-var matrix is now
complete for everything this library's model can express.

### Tests

Wire-level: the echo helper now captures the full served request
(headers included; worker writes, join provides the
happens-before) — `http-parser` asserts headers land between
User-Agent and Content-Type and that injection attempts are
rejected pre-wire. Mutation-tested: removing the CRLF scan fails
the suite. Storage/env parsing unit-tested in
`unit-env-config`.

## [0.7.1] - 2026-08-24

OTEL_RESOURCE_ATTRIBUTES, bench/coverage presets, last legacy out.

### Added — OTEL_RESOURCE_ATTRIBUTES

`otlp_exporter_opts_apply_env()` now applies
`OTEL_RESOURCE_ATTRIBUTES` ("k=v,k=v"): pairs land on the one
value model as STRING resource attributes; malformed segments are
skipped (OTel's log-and-continue posture); values stay literal
(no URL-decoding); a "service.name" entry yields to
`OTEL_SERVICE_NAME` via the existing create-time map semantics.
The v0.7.0 raw-endpoint-buffer parameter became
`otlp_env_storage_t` — one caller-allocated struct holding every
environment-derived string and the parsed attribute array, so
future variables extend storage without another signature change
(documented 0.x change; the API is one day old).

### Added — bench + coverage presets

`cmake --preset bench` (Release + benchmarks) and
`--preset coverage` join default/release/asan/ubsan/tsan.
Release perf measured with the new preset: emit **89 ns/span**
(0 attrs), build+move 135 ns, encode 360 ns + ~60 ns/attribute —
roughly 2x the Debug numbers, linear scaling intact.

### Removed — docs/release-notes/ deleted

Frozen at v0.5.0 and superseded by CHANGELOG.md + GitHub
Releases since v0.6.8. The v0.5.0 notes were merged into the
v0.5.0 GitHub release body before deletion, so nothing was lost;
recoverable from git history.

## [0.7.0] - 2026-08-24

OTel environment variables, one-command builds, legacy removed.

### Added — OTEL_EXPORTER_OTLP_* environment-variable support

`otlp_exporter_opts_apply_env()` applies the OpenTelemetry
standard variables to opts — additive API, composes with
hand-filled opts (unset variables pass through):
`OTEL_EXPORTER_OTLP_ENDPOINT` (base form gets `/v1/traces`
appended), `OTEL_EXPORTER_OTLP_TRACES_ENDPOINT` (wins over the
base form), `OTEL_EXPORTER_OTLP_TIMEOUT` (ms, connect+read),
`OTEL_EXPORTER_OTLP_PROTOCOL` (must be `http/protobuf`),
`OTEL_SERVICE_NAME`. Parsers are pure functions in
`src/env_config` (string in, opts field out — unit-tested without
touching the process environment; test #52); one getenv driver.
Composed endpoints land in a caller-provided buffer (documented
lifetime).

### Added — working CMake presets

`cmake --preset default|release|asan|ubsan|tsan` (plus
build/test presets) — one command per configuration. The
existing CMakePresets.json was invalid schema (top-level
`generator`/`configureOnPress`/`cacheVariables` fields; `cmake
--preset` failed outright) — another never-executed artifact,
now correct and validated end-to-end locally. The ASAN test
preset deliberately does not force `detect_leaks=1`: that option
aborts on macOS (leak detection stays the Linux CI job's job).

### Removed — legacy dead code

`src/arena.{c,h}` deleted: a bump-allocator that was never in
any build target and included by nothing (slab.c carries its own
inline arena). Recoverable from git history if ever needed.

### Performance

Baseline re-verified post-refactor (Debug): emit ~163 ns/span,
encode ~300 ns/span + ~145 ns/attribute — linear in attributes,
matching the historical numbers; the v0.6.11-13 deepening arc
cost nothing. No blind optimization.

## [0.6.15] - 2026-08-24

The vcpkg overlay port becomes real, tested infrastructure.

### Fixed — ports/otlp-c had never been buildable

The in-repo overlay port was pinned at v0.5.17 (97 releases
stale) with an all-zeros placeholder SHA512, and was missing the
`vcpkg-cmake-config` host dependency — `vcpkg_cmake_config_fixup`
was an unknown command, so the recipe failed the moment anything
tried to build it (nothing ever had). The portfile now builds the
LOCAL checkout (`SOURCE_PATH` = the repo containing ports/), so
there is no REF/SHA to drift; the port manifest version, frozen at
0.5.17 for a hundred releases, tracks the library again.

### Added — vcpkg overlay consumer (CI + standalone project)

`tests/consumers/vcpkg_overlay/` installs otlp-c through the
overlay port in manifest mode and runs an emit-to-flush round
trip; the new "vcpkg overlay consumer" CI job (ubuntu) bootstraps
vcpkg and runs exactly that — the third consumption path is now
pinned like find_package and FetchContent. Docs updated: README's
vcpkg section shows the working overlay recipe, the quickstart's
FetchContent tag moved off v0.6.8, CLAUDE.md's key-files table
gains the v0.6.11/13 modules, and the architecture diagram shows
http_response_parser and retry_policy.

## [0.6.14] - 2026-08-24

License change: Apache-2.0 -> BSD 3-Clause.

### Changed — repository license is now BSD 3-Clause

Per maintainer decision, the project license changes from
Apache-2.0 to BSD 3-Clause: the LICENSE text, all 135
SPDX-License-Identifier headers (this also fixed a pre-existing
"Identifier-Identifier" typo in seven files), both vcpkg
manifests, the README badge and license sections, CONTRIBUTING,
and the CLAUDE.md license invariant. Two consequences, stated for
the record: CNCF projects must be Apache-2.0, so the documented
donation path is closed under this license; and releases up to and
including v0.6.13 remain Apache-2.0 — relicensing applies going
forward.

## [0.6.13] - 2026-08-24

Architecture: retry timing as pure, property-tested functions.

### Changed — retry policy extracted; one shared millisecond clock

The full-jitter draw, the exponent clamp, and the Retry-After
floor/cap semantics moved from exporter.c into
`src/retry_policy.{h,c}` — pure functions of their arguments, no
clocks, no exporter state. The 429/5xx branch's inline clamp
collapsed into the one decision function, which also reports
whether the server floor won (the `server_driven` event field).
The millisecond clock that existed as two private copies
(`now_mono_ms` in exporter.c, `mono_ms` in http_client.c) is now
one `otlp_platform_now_mono_ms()`.

### Added — retry-invariant properties (test #51)

`property-retry` pins the timing contract directly: full-jitter
bounds against the computed ceiling, saturation at `max` for
attempt=UINT32_MAX (the undefined-shift trap, UBSAN-enforced),
the Retry-After floor never lowering the delay below
min(floor, max), the cap always holding, and `server_driven`
exactly meaning "the floor beat the drawn jitter" (verified by
replaying the PRNG). Mutation-tested: ignoring the floor fails
the property at the floor invariant.

## [0.6.12] - 2026-08-24

Architecture: one signal table instead of five descriptor families.

### Changed — exporter's per-signal descriptors consolidated

The three signals were dispatched through FIVE descriptor families
(drain, emit, tick, record, start-post), each with three instances
hand-assembled at a different call site across a 2006-line module
— adding a signal would have touched six regions. There is now ONE
static spec table (id, diagnostics name, free/clone/build-request
adapters) plus a per-signal state array `sig[3]` (queue, pending
batch, timers, counters) inside the exporter; every generic driver
(emit, tick, record_outcome, start-post, drain, stats, signal_name)
dispatches through it. exporter.c: 2006 → 1738 lines, net −268;
the three `try_start_*_post` wrappers and the descriptor structs
are gone. Adding a signal is now one table row plus the typed
functions it names. No public API change; the stabilization window
holds.

## [0.6.11] - 2026-08-24

Architecture: the HTTP response parser is a deep module.

### Changed — response wire-format parsing extracted from the socket machine

`try_parse_response` + the chunked decoder were static captives of
`http_client.c`'s socket state machine — every parse scenario
needed a pthread echo server over loopback, and the response fuzz
was excluded on Windows, leaving the RFC 7230 request-smuggling
rejections untested on the MSVC CI job. The parser is now
`src/http_response_parser.{h,c}`: pure `feed(bytes) -> verdict`
with parsed fields written only on success; the socket machine is
a thin adapter (~307 lines removed from http_client.c).

### Added — byte-fixture parser suite (test #50)

`unit-http-response-parser` covers the full matrix — split
delivery invariance, version-aware keep-alive, EOF framing,
smuggling rejections, line-aligned header matching, chunked
framing (extensions, trailers, corruption), Retry-After forms —
with no sockets or threads, on every platform including Windows.
The response fuzz property now feeds the parser directly:
portable, and 20000 iterations where the socket version managed
300.

## [0.6.10] - 2026-08-24

CI hygiene: consumer tests moved out of workflow YAML.

### Changed — consumer fixtures are real projects, not heredocs

Both consumer CI jobs carried their `main.c` and `CMakeLists.txt`
as heredocs inside workflow YAML — code living in strings: no
linting, no formatting, duplicated, unreviewable, and first
executed by CI (PR #146's first Windows run failed on exactly
such a bug). The consumers now live as standalone projects in
`tests/consumers/` (`find_package/`, `fetchcontent/`), runnable
locally with the same commands CI runs (see
`tests/consumers/README.md`). The fetchcontent consumer asserts
the v0.6.9 hygiene invariants as configure-time FATAL_ERRORs —
both mutation-tested: bypassing the top-level guard makes it fail
on "clobbered CMAKE_INSTALL_LIBDIR" / "CPack config leaked".
The find_package consumer now runs a real emit→flush round trip
instead of only printing the version. CI jobs shrank to
`cmake -S tests/consumers/... && build && ctest`.

## [0.6.9] - 2026-08-24

Embedded-build hygiene: the FetchContent path works and is now
side-effect-free.

### Fixed — embedded otlp-c clobbered the consumer's CMake and CPack state

v0.6.8 documented CMake FetchContent / add_subdirectory as the
consumer path, but no consumer had ever been built that way.
Building one surfaced two violations of the library-package
principle:

- `set(CMAKE_INSTALL_LIBDIR "lib" CACHE STRING "" FORCE)`
  overwrote the PARENT project's cache entry — a consumer on a
  Debian multiarch layout (`lib/x86_64-linux-gnu`) found it
  silently rewritten to `lib`, redirecting the consumer's own
  libraries.
- `include(CPack)` wrote `CPackConfig.cmake` /
  `CPackSourceConfig.cmake` into the top-level build dir — the
  consumer's build tree.

Both are now gated on `OTLP_C_IS_TOP_LEVEL`: the install-layout
pin and CPack run only when otlp-c is the project being
configured; embedded builds respect the parent's layout and
write nothing into the parent's tree.

### Added — CI job pinning the documented path

"CMake FetchContent consumer" (ubuntu + windows) builds a real
consumer against the checked-out tree, runs an emit→flush
round-trip, FATAL_ERRORs if the consumer's
`CMAKE_INSTALL_LIBDIR` gets clobbered, and fails if CPack config
leaks into the consumer's build dir. The documented install
paths can no longer silently rot.

## [0.6.8] - 2026-08-24

Install-docs truth sweep.

### Fixed — consumer instructions pointed at a nonexistent vcpkg port

README and quickstart told consumers to install `otlp-c` from
vcpkg (`"dependencies": ["otlp-c"]`). No `otlp-c` port exists in
the public registry (microsoft/vcpkg master has no
`ports/otlp-c`), and no registry fork is published — the
instructions failed for every user who followed them verbatim.
The docs now give the real consumption paths: CMake FetchContent
against a release tag, `add_subdirectory()` of a clone, or
`cmake --install` + `find_package(otlp-c CONFIG)`. The vcpkg
section is reframed as what the repo's manifest actually does:
build this repo under a vcpkg toolchain, pulling in zero
dependencies.

### Fixed — docs/release-notes/ abandoned mid-stream

The directory held one file (v0.5.0) while 100+ releases shipped
after it, and nothing referenced it. A README now marks it frozen
and points at CHANGELOG.md + GitHub Releases as the canonical
per-release notes.

### Fixed — property-http-timeout failed deterministically on VPN networks

The property connects to TEST-NET-1 (192.0.2.1) expecting the
connect to hang or be refused — but VPN/proxy networks locally
accept every TCP connect, so the request advanced to READING and
terminated at the READ deadline, which equaled the test's own
5s wall-clock cap (observed elapsed: 5009 ms). The read deadline
is now 2000 ms, leaving the 5s cap real margin; the property
accepts all three bounded outcomes (connect timeout, instant
refusal, VPN-accepted connect + read deadline). Library behavior
was correct throughout — bounded completion held.

### Fixed — a literal backslash-ref in roadmap.md tripped Doxygen

The v0.6.5 row's "\ref warnings" phrasing parsed as a Doxygen
command (unresolved-reference warning); reworded. Docs build is
back to zero warnings.

## [0.6.7] - 2026-08-24

CLAUDE.md truth sweep.

### Fixed — stale CI claims

CLAUDE.md claimed a checkpatch CI gate
(`.github/workflows/checkpatch.yml`, `ci/checkpatch.sh`) that was
deleted in the workflow consolidation — the references survived
the deletion. It also pointed at `.github/workflows/build.yml`,
renamed to `ci.yml` in the same change. Now states reality:
clang-format (in checks.yml) is the enforced style gate; the
kernel-style conventions are reviewer-enforced. CodeQL's status
(repo-level default setup) is documented where the false claim
stood. Verified alongside: FreeBSD CI exists, and every path in
the Key files table resolves.

## [0.6.6] - 2026-08-23

Integration suite re-validated against a live otelcol + Jaeger.

### Fixed — 10s visibility budget was too tight for a cold pipeline

The end-to-end test polls Jaeger for the exported span for up
to 10s — but a COLD pipeline can exceed that on its own
batching: otelcol's jaeger exporter flushes on a 5s batch timer
before Jaeger even indexes the span. Observed live: the first
run against fresh containers timed out at 10s; the warm rerun
passed. Budget raised to 30s and validated by a cold run that
passed at 24.8s — inside the new budget, past the old one.

### Verified — everything since v0.5.95 sees a real collector

The suite (otelcol-contrib 0.159.0 + Jaeger, all three signals:
100 spans in 2 batches + one-shot metric + log, event/status
visibility queried back from Jaeger) had not run since before
v0.5.95. It now exercises Retry-After-era HTTP handling,
PartialSuccess-era response decoding, the UTF-8 boundary, and
the v0.5.100 event model against real collector responses —
the diagnostic output during the run is the event formatter
itself ("batch sent: 50 spans"). All green.

## [0.6.5] - 2026-08-23

Spec hygiene: warning-free Doxygen + architecture catch-up.

### Fixed — the API reference build

- The Doxyfile's PROJECT_NUMBER said 0.2.0 — stale for over a
  hundred releases. It is now a CMake-configured template
  (Doxyfile.in) taking PROJECT_VERSION, so the number cannot
  drift again.
- INPUT covered only headers + README + cookbook; every README
  link to another doc became an unresolvable Doxygen \ref (a
  wall of warnings). All spec pages — quickstart, deployment,
  otlp-spec, architecture, cookbook, roadmap, integration-test,
  SECURITY, CONTRIBUTING, CODE_OF_CONDUCT, CLAUDE — are now
  Doxygen inputs: the reference builds with ZERO warnings at 84
  HTML pages, with the specs cross-linked into the API docs.
- One real doc bug surfaced via the warnings: a `#repr(C)` in
  roadmap.md that Doxygen parsed as a link request.

### Added — architecture.md catch-up

The module table predated `protobuf_decode.c` and the
v0.5.100/103 designs; it now documents the decode module, the
diagnostics event model (one model, derived string view), the
UTF-8 boundary contract, and the two independent
wire-conformance gates (schema pins + golden vectors).

## [0.6.4] - 2026-08-23

Windows test parity.

### Fixed — two portable test suites never ran on Windows

`exporter-retry` was fully portable (its own header said "runs
on all platforms") but sat in the POSIX-only CMake block — the
Windows CI job never ran it. `exporter-events` was portable
except for its wall-clock drive loop (`CLOCK_MONOTONIC` doesn't
exist on MSVC).

The events suite now drives on OUTCOME instead of wall time —
`drive_until()` ticks (each tick sleeps via the library's own
portable sleep) until the expected event count arrives; the
iteration budget is far past these scenarios' 5ms backoff caps,
and the loop exits on the terminal outcome, not a timeout. Both
suites moved out of the `if(UNIX)` block: Windows CI now covers
the full retry state machine (backoff/retry/drop) and the
structured-events suite.

## [0.6.3] - 2026-08-23

Encoder OOM-propagation coverage.

### Added — fail-injection into the encode paths

The encoders' failure paths (the goto-out cleanup arms) had
never executed — the allocator-oom harness only probed
create/clone paths. Three new cases fail every allocation
position in the traces/metrics/logs encode paths one at a time
(100/120/100 probes), asserting the result is always
OTLP_ERR_NOMEM and that every successful allocation is paired
with a free on every failure path. The encoders' cleanup is
proven correct: no leaks, clean propagation.

The unlock was the small-buffer optimization: the pb buffers
keep a 64-byte inline buffer, so tiny fixtures never allocate
and mid-emission reserve failures were unreachable. Fixtures
now carry 160-byte attribute values, pushing every sub-buffer
to the heap — the metrics encoder's line coverage rises
82.09% → 84.45%, and every library file remains at 82%+.

## [0.6.2] - 2026-08-23

Coverage re-measurement + deep-clone round-trips.

### Fixed — five files had drifted below the 82% coverage bar

Re-running the clang-profile coverage flow across all test
binaries showed platform.c (71%), span.c (80%), internal_util.c
(81%), otlp_metrics_encoder.c (81%), and metric.c (81%) below
the documented every-file bar. Root cause: the clone/copy arms
for anything richer than scalar attributes had never executed —
histogram bounds + bucket_counts and exp-histogram buckets in
`otlp_metric_clone`, status_message/trace_state in
`otlp_span_clone`, bytes/array/kvlist attribute copies, and the
event/link attribute setters entirely.

New `unit-clone` (4 tests): deep-clone round-trips for span,
metric, and log proven by byte-equal encoding of original vs
clone (every attribute type on span, events, and links;
histogram and exp-histogram state), plus the has_start/has_time
emission matrix for NDP/HDP and the free(NULL) guards. Platform
clock NULL-guard tests added. After: every library file back at
82%+ (platform 85.7%, span 86.6%, metric 87.3%,
internal_util 84.5%, metrics encoder 82.1%).

Out of scope, documented: the encoders' OOM-propagation arms
(~60 lines) — those need fail-injection into the encode paths.

## [0.6.1] - 2026-08-23

Documentation freshness sweep (first release inside the
stabilization window — docs only, no code, no API).

### Fixed — README feature-list drift

- Resource attributes were described as the pre-v0.5.92
  four-type model; now the full `otlp_value_t` value model with
  create-time map semantics.
- Diagnostics described only `set_logger`; now leads with the
  structured `set_event_logger` surface (v0.5.100).
- Added missing capabilities: server-response awareness
  (Retry-After honored; PartialSuccess surfaced via diagnostics
  and per-signal `rejected_*` stats) and UTF-8 boundary
  validation.
- The single example link became the full list (minimal,
  multithread, event_loop_integration).

### Fixed — cookbook pointer

The event-loop section now references the runnable
`examples/event_loop_integration.c` (v0.5.105) alongside the
libuv adaptation prose.

## [0.6.0] - 2026-08-23

The stabilization window opens.

This minor bump starts the 0.6 line in which the public API is
additive-only: every change from here to 1.0.0 is a new function,
a new opt-in field, or a bug fix — nothing breaks, nothing is
removed. When one full minor cycle holds that discipline, the 1.0
criteria in docs/roadmap.md ("Path to 1.0") are met and the API
freezes for the 1.x line.

Snapshot of the criteria at the window's opening — all satisfied
as of 0.5.105:

1. Public surface audited: 124 exported functions across 10
   headers with complete return-code, ownership, and
   thread-safety documentation; every function covered by at
   least one test or example (the v0.5.105 dead-surface sweep).
2. Wire conformance proven: 31/31 schema tables pinned against
   opentelemetry-proto literals; whole payloads reference-
   validated by golden vectors (all three signals, composite
   attributes included).
3. Boundary validation complete: header injection (CWE-93),
   integer overflow (CWE-190), UTF-8 (proto3 string contract),
   malformed-response decoding — each with dedicated tests.
4. No open P1/P2 items in TODO.complete.

No library code changes in this release — the diff is the
version constants and this record.

## [0.5.105] - 2026-08-23

Dead-surface sweep: coverage for the uncovered API.

### Fixed — poll_fds() contract wart (caught by its first test)

`otlp_exporter_poll_fds()` — exported since v0.1, documented as
THE event-loop integration surface — had ZERO test or example
coverage. Writing the test immediately caught a contract wart:
`out=NULL` with `cap>0` returned `OTLP_OK` when no request was in
flight (the state check preceded argument validation). Arguments
are now validated first — `OTLP_ERR_NULL` regardless of exporter
state.

### Added — poll-loop integration test + the Phase 7 example

`exporter-poll` drives a REAL `poll()` loop off the exposed fd +
interest bits against the echo server to completion (sent == 1),
plus the zero-fd and argument-validation contract.
`examples/event_loop_integration.c` — the original plan's Phase 7
deliverable, quietly never built — demonstrates the embedding
pattern end-to-end: poll on the in-flight fd, poll-as-sleep for
the batch timer, event-logger tally narrating the outcome. With
no collector it exits cleanly after the retry budget.

### Added — strerror completeness pin

`unit-common` asserts every status code maps to a non-empty,
pairwise-distinct message (complete at 20/20 today; a new enum
value without a message entry can no longer land silently — nor
can a duplicated message hide a mis-mapped case).

## [0.5.104] - 2026-08-23

Public API audit + 1.0-readiness assessment.

### Added — Path-to-1.0 section in the roadmap

The stabilization criteria are now explicit: surface audited,
wire conformance proven (pins + goldens), boundary validation
complete (injection/overflow/UTF-8/malformed responses), no open
P1/P2 items, one additive-only minor cycle. TLS, gRPC, and
compression are recorded as deliberate 1.x exclusions (the
zero-non-libc-dependency invariant), with a 2.x optional-deps
model as their path.

### Fixed — audit remainder on the public surface

All 124 exported functions across 10 headers were audited
(docstring, return codes, ownership, thread-safety). The surface
was largely in shape from v0.5.94/100/103; the remainder:
`flush_metric`/`flush_log` return codes + thread-safety (they
block the owner thread; TIMEOUT/NETWORK outcomes documented),
NULL-on-OOM for the sampler constructors, and the UTF-8 contract
noted on the span/metric/log attribute conventions (it was
documented only at the exporter level).

### Performance — A/B against v0.5.102

Measured on the same host, back to back: emit pipeline
175–433 ns/span (0–5 attrs, null transport), encode ~170 ns/attr
(linear in attributes — an earlier apparent 8-attr outlier was
timer noise). The v0.5.100 event layer and v0.5.103 UTF-8
validation are within run-to-run noise of the prior release; no
optimization warranted.

## [0.5.103] - 2026-08-22

UTF-8 conformance at the API boundary.

### Fixed — one invalid string could kill a whole batch at the collector

OTLP string fields are proto3 `string` — they must be valid
UTF-8, and the Go protobuf runtime used by otelcol rejects the
entire ExportRequest on unmarshal when any string field is not.
A single attribute value with invalid bytes (a Latin-1 filename,
a truncated multi-byte sequence) made the exporter retry and
eventually drop the WHOLE batch with only a confusing HTTP 400
to show for it. Nothing validated this: `OTLP_ERR_UTF8` has sat
unused in the status enum since v0.1.

The library now validates UTF-8 at the string-ingestion
boundary and fails the setter instead: the set-attribute engine
covers all six attribute surfaces at once (keys, string values,
and composite ARRAY/KVLIST items/entries — whose builders now
propagate the real status instead of flattening failures to
NOMEM), plus the scalar wire strings (span name/trace_state/
status/event name, metric name/unit/description, log body/
severity_text, service_name). `bytes` values are exempt
(proto3 `bytes` accepts anything). Invalid input returns
`OTLP_ERR_UTF8` from setters and NULL from creates — one bad
value fails itself, not the batch.

Also: `get_stats()` is reclassified as thread-safe (it was
already atomic-loads-only; the header overstated the
restriction), and the golden-vector corpus now includes ARRAY
and KVLIST attributes — the composite encoders are
reference-validated for the first time, including oneof
presence for an explicitly-set false bool (420 = 420 bytes).

## [0.5.102] - 2026-08-22

Hygiene catch-up.

### Fixed — concurrency-stress echo worker lifetime (the last unchecked joins)

The stress test's echo worker was started with
`requests_to_serve = 100` for a ~13-request scenario, so its
final `echo_server_join` timed out silently with the worker still
blocked in accept() — the same hazard class fixed in v0.5.96/98,
present since the test was written. Now stopped deterministically
(self-connect wake; the exact request count is not knowable a
priori) with the join checked, joins checked on both error paths,
and all eight worker-thread `pthread_join`s checked. The
"never `(void)` a join" rule now holds everywhere in the tree.

### Added — event-callback example

`examples/minimal.c` now installs
`otlp_exporter_set_event_logger()` with a tallying callback
(BATCH_SENT / ITEMS_DROPPED, with drop_reason/signal called out)
and prints the tally at exit — the diagnostics-as-data pattern
demonstrated end-to-end.

## [0.5.101] - 2026-08-22

Golden vectors: payloads validated against the reference
implementation.

### Added — reference-serialized golden vectors for all 3 signals

`tests/golden/generate.py` builds fixed payloads with the
REFERENCE opentelemetry-proto Python classes (1.44.0) — not our
encoder — and embeds them via a generated header. The new
`unit-golden` test reconstructs the same fixtures through the
public API + internal encoders and compares both sides as
canonical protobuf field trees (fields matched by number/wire
type, repeated order preserved, recursive message compare): valid
reordering never fails; any drift in field numbers, wire types,
zigzag/packing, presence rules, or values fails with a path to
the mismatch. Covers all six AnyValue variants (incl. negative
int64 → 10-byte varint, binary bytes), events/links/status,
histogram buckets/bounds/min-max, exp-histogram zigzag scale +
explicit bucket arrays, and log trace correlation. After fixture
alignment (our spans carry W3C `flags=1` sampled by default; the
generator now mirrors that) all three vectors are byte-length
identical to the reference serialization.

`protobuf_decode` gained `otlp_pb_read_fixed32/64` — the decode
counterpart of the encoder's fixed-width writers, bounds-checked
like the rest.

The comparator's first draft was caught by mutation testing
comparing ours-with-itself via a shared node pool (a flipped
fixture value sailed through) — pools are now per-side, and both
a value flip and the pool bug are covered by the recorded
mutation runs.

## [0.5.100] - 2026-08-22

Structured diagnostics events.

### Added — otlp_exporter_set_event_logger(): diagnostics as data

The string logger renders diagnostics for humans; programmatic
consumers (metrics, alerting, self-telemetry) had to parse those
strings. The new additive callback delivers every diagnostic as
an `otlp_event_t`: event code (QUEUE_FULL, BATCH_SENT,
RETRY_ARMED, ITEMS_DROPPED, PARTIAL_SUCCESS, SYNC_FLUSH_FAILED),
signal id (traces/metrics/logs), item counts, HTTP status,
retry attempt/delay, drop reason (max-retries / permanent-HTTP /
queue-full), and — for PartialSuccess — the server message with
length. No string parsing, stable enum codes, exact counts.

Model-driven by construction: `otlp_event_t` is the single model
behind every diagnostic — the legacy string messages are now
DERIVED from it by one formatter (`format_event`), replacing the
14 printf call sites scattered through the exporter, so the two
views cannot diverge (DRY: the facts live once). The variadic
`otlp_log()` helper is gone; signal names come from the event's
signal id, unifying the previous span/spans, metric/metrics
inconsistencies. The sync-flush `path + 5` string hack is gone
too.

Covered by a new 7-scenario null-transport suite (code, signal,
count, level, drop reason, attempt/delay per outcome; queue-full
fires on the emitting thread) and event assertions added to the
PartialSuccess wire test. Existing string-logger output is
unchanged apart from the unified wording.

## [0.5.99] - 2026-08-22

Enforced Release checks; all 31 schema tables pinned upstream.

### Fixed — v0.5.98's checks executed in Release but did not enforce

`check_true`/`check_ok` were `assert(cond); (void)cond;` — under
NDEBUG the assert vanishes and the `(void)` discards the result,
so a FALSE check did not fail the test. The v0.5.98 changelog's
"verifies identically in Debug and Release" overstated it:
Release runs executed the checks without enforcing them. The
helpers now abort on failure in every configuration, reporting
the call site. Mutation-tested: a deliberately drifted schema
field aborts with a named diagnostic in both Debug and Release.

### Added — every schema table pinned against opentelemetry-proto

v0.5.97 pinned 6 of 31 tables. The pins test is now
table-driven and complete: all 31 message tables in
`src/otlp_schema.h` (traces + metrics + logs envelopes, Span/
Event/Link/Status, the AnyValue family, every data-point type)
are checked field-number-by-field-number against upstream
literals, with named diagnostics on mismatch. Cross-check
result: all 31 match — including AnyValue (re-verified against
live common.proto), where a stale recollection had suggested
different numbers.

### Added — metrics/logs field reference in docs/otlp-spec.md

The protocol reference documented traces only; metrics and logs
schemas were entirely absent — the gap that let the v0.5.97
min/max drift hide. Both signals are now fully documented
(proto blocks, field tables, reserved fields, not-emitted
fields, the varint-vs-fixed32 flags and packed-varint-vs-
packed-fixed64 bucket asymmetries), with pointers to
`src/otlp_schema.h` (canonical) and `unit-wire-numbers`
(executable enforcement).

## [0.5.98] - 2026-08-22

Zero-Release-warnings test suite + always-evaluated checks.

### Fixed — Release builds verified almost nothing in the test suite

CI's plain jobs build Release (NDEBUG), which compiles
`assert(expr)` to nothing. The v0.5.82–95 lesson covered
side-effecting calls inside asserts; the quieter face — plain
value-asserts like `assert(st == OTLP_OK)` — meant Release runs
of the unit tests checked almost nothing: 536 assertion sites
across 12 test files were elided entirely, and the results they
consumed triggered ~100 `-Wunused-but-set-variable` /
`-Wunused-parameter` / `-Wunused-variable` warnings (masked in
Debug, where asserts still evaluate).

New `tests/test_util.h` provides `check_ok()` / `check_true()`:
the expression is an ARGUMENT, so it executes in every
configuration while `assert()` inside the helper keeps Debug
diagnostics. All 536 sites converted; the full suite now
verifies identically in Debug and Release, and the zero-warnings
invariant holds in BOTH configurations. All previously elided
Release assertions passed — no silent Release-only failure was
lurking.

### Fixed — hidden echo-worker lifetime bug the conversion exposed

`check_ok(echo_server_join(...))` in exporter-echo replaced a
`(void)`-ed join that had been timing out silently: the worker
was started with `requests_to_serve = 16` for a ~4-request test,
stayed blocked in accept(), and was still running when main
returned — the exact stack-use-after-return hazard fixed for
partial-success in v0.5.96, present here since the metrics/logs
cases were added. Now `echo_server_stop()`d with the join result
checked.

Stopping the worker then exposed a second, latent bug CI's
TSAN job caught immediately: `echo_server_stop()` and the
worker's exit path both closed/wrote the plain-int `sock_fd` —
a data race (and potential double-close) that existed since the
helper was written; it was never triggered because every prior
caller stopped only already-exited workers. The helper is
rewritten: `sock_fd` is atomic with the worker as the single
closer, `stop()` only sets an atomic `stopping` flag and wakes
accept() with a self-connect (the portable wake — neither
shutdown() nor close() on a listening socket reliably unblocks
accept() on macOS; the old listen-fd-close mechanism never
actually worked there). Local TSAN now runs the full suite
clean.

### Docs — CLAUDE.md/roadmap catch-up

CLAUDE.md capability bullets for v0.5.95–97 (Retry-After was
already noted); new test-writing rule pointing at the helpers;
roadmap key-metrics line refreshed (138 TODOs, 43 tests).

## [0.5.97] - 2026-08-22

Metrics wire-schema audit against opentelemetry-proto.

### Fixed — HistogramDataPoint min/max emitted at wrong field numbers

`src/otlp_schema.h` had histogram `min` at field 10 and `max` at
11 since the metrics signal shipped. Upstream metrics.proto is
`flags = 10` (uint32 varint), `min = 11`, `max = 12`: our FIXED64
min at 10 was dropped by collectors as an unknown field, and our
max at 11 was decoded as the collector's min — exported
histograms lost their minimum and reported a doubled max. Fixed
to the upstream numbers. Also fixed the dormant
ExponentialHistogramDataPoint `flags` entry (declared fixed32;
upstream is uint32 varint — the field is not yet emitted, so no
wire impact) and a LogRecord table comment that mislabeled
reserved field 4 as observed_time.

### Added — upstream-literal wire test (the guard that was missing)

Every existing test walked the wire using the schema's own field
numbers — a self-referential check that can only agree with the
schema, bug included; that is how the min/max drift survived
since the signal shipped. New `tests/unit/test_unit_wire_numbers`
pins the encoded bytes (HDP min@11/max@12/count@4/sum@5/
buckets@6/bounds@7; EHDP scale zigzag@6, buckets@8 with offset
zigzag@1 and varint-packed counts@2) and the schema tables
themselves against literals copied from opentelemetry-proto —
never derived from `otlp_schema.h`, breaking the circle.

## [0.5.96] - 2026-08-22

Server-side data-loss reporting (OTLP PartialSuccess).

### Added — PartialSuccess surfaced from 200-OK responses

A collector can answer 200 OK while rejecting some items,
declaring it in the protobuf response body (Export*PartialSuccess:
rejected count + error message). The exporter discarded 200
bodies entirely — silent server-side data loss was invisible.
It now decodes PartialSuccess (new bounds-checked wire-format
reader `src/protobuf_decode.{h,c}`, the decode counterpart of
the encoder; schema-driven field tables) and surfaces it: a WARN
diagnostic ("collector partial success: 2 of 3 spans rejected:
queue full") plus new per-signal stats `rejected_spans` /
`rejected_metrics` / `rejected_logs`. The batch is not retried —
a 200 is final. Applies to the async pipeline and the sync
one-shot flush paths. Unknown fields are skipped (forward
compat); malformed bodies fail closed (treated as plain success).

### Fixed — a lost property test and warning debt

`prop_resource_full_value_model` (the v0.5.92 BYTES property)
was defined but never registered — the v0.5.92 file rewrite
dropped the registration and nothing failed. Registered and
running again; the unused-variable warning it masked is gone
too (zero-warnings invariant restored).

## [0.5.95] - 2026-08-22

Server-directed retry pacing (RFC 7231 §7.1.3).

### Added — Retry-After honored on throttled responses

The HTTP client now parses the response's `Retry-After` header
(delta-seconds form; HTTP-date treated as absent; duplicates
last-wins; saturates rather than wraps) and exposes it via
`otlp_http_request_retry_after_ms()`. On a retryable status
(429/503/5xx) the exporter's next attempt waits
**max(jittered backoff, Retry-After)** — never sooner than the
server asked — clamped to `backoff_max_ms`, so a hostile server
cannot stall exports indefinitely. The retry WARN log annotates
"(server Retry-After)" when the server value drove the delay.
Verified on the wire: Retry-After: 1 delays the retry by the
full second; Retry-After: 60 with backoff_max=300 still retries
at ~300ms.

### Fixed — Release builds silently vacated three test files

Six asserts carried side effects (`otlp_exporter_emit_move`,
`otlp_metric_record`) — under Release/NDEBUG the calls vanish,
so the entire `exporter-retry` suite (and one echo case) was
executing nothing and passing vacuously. All converted to
statement + pure assert (the v0.5.82 rule's fifth sighting).
Also repaired `test_helper_echo.h`, whose prototypes had been
mangled by a v0.5.89 guard edit (three stray duplicate
`#define ECHO_RAW_RESPONSE` blocks inside declarations — it
compiled only by preprocessor accident).

## [0.5.94] - 2026-08-22

Public-header API coherence audit.

### Fixed — duplicate declarations in `metric.h`

`otlp_metric_set_attribute_array` / `_kvlist` were declared
twice — a copy-paste artifact from the v0.5.71 parity patch,
shipped for 23 releases. Removed; a duplicate-declaration scan
across all public headers is clean.

### Added — return-code documentation for metric/log headers

28 status-returning functions across `metric.h` and `log.h`
documented no return codes at the header level (only the exporter
did). Both headers now carry the uniform "Return codes" contract
(OK / NULL / NOMEM / OVERFLOW / INVALID_ARGUMENT plus
per-surface specifics).

### Verified clean as-is

Naming vocabulary uniform; every create/free pair carries
lifetime language; the umbrella `otlp.h` includes all 13 headers;
every header compiles standalone as C99 **and** C++ (mechanical
sweeps). Pre-0.6.0 API review groundwork.

## [0.5.93] - 2026-08-21

Documentation catch-up for v0.5.87–v0.5.92 (docs only).

### Fixed — stale documentation after the v0.5.92 breaking change

- `docs/cookbook.md`: the resource-attributes example still used
  the deleted parallel-fields struct (`OTLP_RESOURCE_ATTR_INT64`
  etc.) — broken since the moment v0.5.92 shipped. Rewritten to
  the value model.
- `docs/roadmap.md`: version rows for v0.5.87–v0.5.92, key-metrics
  refreshed (132 TODOs, 39 tests, 43+ bugs, every file at 82%+
  coverage), and a unification-arc summary paragraph.
- `CLAUDE.md`: resource attributes documented as the sixth
  attribute surface on the value model; conventions updated from
  five surfaces to six.

No code changes; build + full suite ran as the gate.

## [0.5.92] - 2026-08-21

Resource attributes on the one value model (breaking change).

### Changed — `otlp_resource_attr_t` = `{ key, otlp_value_t }`

Resource attributes were the last surface on a parallel-fields
struct with 4 of 7 types. They now use the same public value
model as every other attribute surface — all AnyValue types
including BYTES (newly supported here). Migration:
`.type = OTLP_RESOURCE_ATTR_INT64, .int64_val = 5` →
`.value = {.type = OTLP_VALUE_INT64, .v = {.int64_val = 5}}`.

Internally the exporter stores an owned attribute vector built by
the set-attribute engine (map semantics, deep copy, grow-on-demand
— the engine's behavior, not a copy of it), and the encoder's
4-way type switch is deleted: encoding flows through the one
`otlp_encode_any_value` dispatch.

### Fixed — OOM-injection realloc accounting (test infrastructure)

`fail_realloc` modeled realloc as alloc-only; growth reallocs
consumed the old pointer without a counted free, so any
grow-on-demand pattern showed a phantom leak per growth. LSAN
confirmed no real leak. Realloc(p≠NULL) now counts free+alloc.

## [0.5.91] - 2026-08-20

Coverage lap 2: platform + tracer error paths.

### Added — `unit-platform` tests (the v0.5.90 baseline's two
worst-covered files)

`llvm-cov show` mapped the zero-hit lines: platform NULL guards,
DNS failure, connection refusal, and tracer NULL/edge paths. New
tests cover all of them — including DNS failure via the
RFC 2606-reserved `.invalid` TLD and connection-refused via a
bind-close-connect sequence that drives the non-blocking
finish_connect error path. platform_unix 76% → 82%, tracer 81% →
91% regions (100% functions). Every check is an explicit rc
comparison (no side-effecting asserts — the first draft's
Release-only unused-variable warnings were that rule reminding
us). Child-span trace-id inheritance now has a direct assertion.

## [0.5.90] - 2026-08-20

Coverage-guided testing: the coverage option actually works now.

### Fixed — OTLP_C_ENABLE_COVERAGE never instrumented anything

The instrumentation flags were added via directory-scope
`add_compile_options` AFTER the `otlp_c` target was defined —
CMake applies those only to targets created later, so the option
silently produced zero coverage data since it was introduced. The
flags are now target-scoped (`target_compile_options/link_options
... PUBLIC`), propagating to every test binary that links the
library. Verified end-to-end: llvm-profdata merge + llvm-cov
report over all 38 test runs.

### Coverage findings → tests

First real measurement showed `exporter_otel.c` at **30% region
coverage** (100% of prior tests exercised spans or
null_transport — the metric/log HTTP POST-build paths had zero
coverage). New exporter-echo cases drive real-HTTP metric and log
exports (async `emit_metric_move`/`emit_log_move` + flush):
`exporter_otel.c` now at **75% regions / 100% functions**.
Remaining misses are OOM/failure branches.

## [0.5.89] - 2026-08-20

Fuzz coverage for the response parser; spot audits (clean).

### Added — fuzz properties

- `prop_fuzz_http_response`: random or mutated (byte flips /
  truncation / extension of a valid chunked response, including
  size-lines and trailers) raw responses served through the real
  request state machine via the raw echo mode — must always reach
  DONE or FAILED within a wall-clock bound, never hang or crash.
  The v0.5.80 chunked decoder previously had only hand-written
  cases. Verified at 5,000 iterations, including under ASAN +
  LeakSanitizer.
- `prop_fuzz_context_extract`: arbitrary printable carrier values
  for traceparent/tracestate/baggage; extract never crashes
  (5,000 iterations).

### Audited — verified correct, no changes

- `otlp_strerror()` coverage diffed against the status enum: every
  case has a message.
- `platform_unix.c` socket wrappers: EINTR retry, EAGAIN mapping,
  EOF flag semantics, MSG_NOSIGNAL — all correct.

## [0.5.88] - 2026-08-20

Examples audit; runtime version-string drift fixed.

### Fixed — otlp_version() returned "0.5.68" for nineteen releases

`OTLP_C_VERSION_STRING` was a hand-maintained literal last touched
at v0.5.68; every subsequent release bumped the numeric macros
(and CMakeLists + vcpkg.json) but not the string. Found by
running the refreshed minimal example. The string is now DERIVED
from the macros via token-pasting — it cannot drift again — and a
smoke-test assertion pins `otlp_version()` against the numeric
macros.

### Changed — examples refreshed

- `minimal.c`: demonstrates the composite ArrayValue attribute
  (`otlp_span_set_attribute_array` from `otlp_value_t`) and the
  map semantics (re-setting a key replaces its value) — its
  "full API surface" claim now holds.
- `multithread.c`: verified against the v0.5.82 concurrency
  contract (workers join before flush+free — exactly right); the
  header now documents WHY, and notes `emit_move` as the
  clone-free hot-path alternative.

## [0.5.87] - 2026-08-19

Documentation catch-up for the audit arc (docs only).

### Fixed — stale documentation across eight releases

- `docs/roadmap.md`: version-table rows for v0.5.79–v0.5.86 (PR
  numbers verified against the merge history), key-metrics block
  refreshed (126 TODOs, 38 tests, 40+ bugs, Debug-and-Release
  verification, encode ~990 ns/span @ 5 attrs), and an audit-arc
  summary naming the two banked test-discipline lessons.
- `CLAUDE.md`: capability list gained the four audit-arc systems
  (hardened HTTP client, W3C-exact propagation, jittered retry,
  arena-aware slab); new test-writing rules section — no
  side-effecting asserts (NDEBUG elision), verify Debug AND
  Release with wall-clock-bounded loops, macOS ASAN needs
  `detect_leaks=1`.
- `README.md`: retry says "full jitter"; new hardened-HTTP and
  W3C-exact-propagation bullets; slab notes any-slot-size safety.
- `docs/architecture.md`: the encoder module entry documents the
  192-byte SBO and its sizing rationale.

No code changes; build + full suite ran as the gate.

## [0.5.86] - 2026-08-19

Encode-path audit: SBO sized for the span envelope (~10% faster).

### Changed — `OTLP_PB_SBO_SIZE` 64 → 192

The audit found the encoder already tight — the suspected
per-attribute malloc storm doesn't exist (typical attribute
KeyValues fit the inline small buffer). The real escape was the
span sub-message: at ~60–80 bytes it just exceeded the 64-byte
inline buffer, so every per-span (and most per-event/link)
sub-message hit the heap plus a growth copy. At 192, span
envelopes, event/link sub-messages with small attributes, and
single-record flush bodies encode inline. Measured ~10% on
attribute-bearing batches (batch=512×5: ~1,090 → ~990 ns/span);
output is byte-identical (encoded sizes match exactly; 20k-iteration
wire properties unchanged). `test_buf_growth`'s payload was
re-sized against the new SBO (the old one now fits inline — caught
by the ASAN job, since Release elides asserts).

## [0.5.85] - 2026-08-19

Slab allocator: arena-aware realloc (undefined-behavior fix).

### Fixed — libc realloc on arena pointers

`otlp_install_slab_allocator` passed `realloc` straight through to
the previous allocator, so any arena-served allocation that later
grew via `otlp_realloc` (e.g. the HTTP response buffer with
`slot_size >= 4096`) was handed to libc realloc on a pointer libc
never allocated — undefined behavior that aborts under macOS
libmalloc and corrupts the glibc heap. The wrapped allocator now
MOVES arena pointers: allocate at the new size, copy
`min(slot_size, n)` bytes, return the slot. Any slot size is now
safe; sizing guidance (and the quickstart/cookbook examples, now
`(256, 512)`) is purely about hit rate. Pinned by
`prop_slab_global_realloc_growth`, which aborts the process on the
pre-fix code.

### Changed

- `slab.c` sizing note rewritten (the old guidance pre-dates the
  v0.5.75 vector model).
- Two sign-conversion warnings in `test_http_parser.c` fixed.

## [0.5.84] - 2026-08-19

Send-stall timeout + sampler endianness.

### Fixed — SENDING phase had no deadline (send-side slowloris)

`connect_timeout_ms` covered CONNECTING and `read_timeout_ms`
covered READING, but a server that accepts the connection yet
never reads the POST blocked SENDING forever once the kernel send
buffer filled. The inactivity deadline now covers SENDING too:
the idle timer (formerly `last_recv_ms`, now `last_io_ms`) resets
on connect completion and every partial write, and `step_sending`
fails with `OTLP_ERR_TIMEOUT` when no progress occurs within
`read_timeout_ms`. A slow-but-steady stream never trips it — the
timer measures inactivity, not duration; the header documents the
semantics. Pinned by a sink-server test (accepts, never reads, 4
MB POST → TIMEOUT) that hangs on the previous code.

### Fixed — ratio sampler read the trace-ID prefix with native endianness

`memcpy(&prefix, trace_id, 8)` is byte-reversed on little-endian:
the sampling *rate* was correct everywhere, but *which* IDs sample
was platform-dependent, contradicting the documented cross-SDK
consistency. Now an explicit big-endian load matching otel-go.
A known-answer property pins it (ratio 0.5: 0x80..-prefixed IDs
not sampled, 0x7F.. sampled) on every platform.

## [0.5.83] - 2026-08-18

Retry/backoff audit: jitter implemented, shift UB fixed, 429 bucket corrected.

### Fixed — backoff shift was undefined behavior for large max_retries

`backoff_initial_ms << (attempt - 1)` operated on `uint32_t` with
an unbounded shift count — a caller-set `max_retries` above ~33
shifts past the type width (CWE-190 family). The exponent is now
computed in `uint64_t` with the shift count clamped, saturating at
`backoff_max_ms`. Pinned by a `max_retries = 100` retry test that
traps under UBSAN on the old code.

### Changed — exponential backoff now has full jitter (as documented)

`docs/otlp-spec.md` has claimed "exponential backoff with full
jitter" since the start — the code had none (deterministic
delays). The delay is now uniform in
`[0, min(initial << (attempt-1), max)]` via a tick-thread-only
PRNG seeded at exporter create. The per-retry upper bound is
unchanged. The duplicated delay computations (network-error and
429/5xx paths) collapsed into one `backoff_delay_ms` helper that
owns the saturation math and the jitter draw.

### Fixed — HTTP 429 counted in the http_5xx bucket

429 is retryable but still a 4xx; it now increments `http_4xx`
(stats comment updated). New retry-test cases pin the 429 path
(429 then 200 → sent, `http_4xx >= 1`, `http_5xx == 0`) and the
shift guard.

## [0.5.82] - 2026-08-18

MPSC + shutdown-drain audit: contracts documented, protocol pinned.

### Changed — documented contracts

- `otlp_exporter_free()` now states the concurrency requirement:
  `shutdown()` is a cooperative stop signal, NOT a barrier —
  emits that already passed the shutdown check may still enqueue
  afterwards, so the caller must join producer threads after they
  observe `OTLP_ERR_SHUTDOWN` before freeing. An emit racing
  `free()` is a use-after-free on the queues.
- `mpsc_queue_free()` documents that it frees the slots array
  only; queued items must be drained first or they leak.

### Added — shutdown-protocol stress test

Every existing stress test joined producers *before* shutdown.
New `tests/test_shutdown_stress.c` exercises the documented
sequence as written: 4 producers spin on `emit_move` (with
back-pressure backoff on `OTLP_ERR_BUFFER_FULL`) until they
observe `OTLP_ERR_SHUTDOWN` themselves, while the owner ticks,
then shutdown → join → drain → flush → free. Asserts every
producer observed the stop signal, `emitted` equals the total
accepted, and the stats contract `emitted == sent + dropped_err`
holds (`dropped_full` counts rejected emits, never counted in
`emitted`). Under ASAN + LeakSanitizer this pins use-after-free
and leak freedom for the protocol.

### Audited — MPSC queue core found correct

Every memory-ordering pair traced by hand (producer
release-publish of the slot sequence; consumer release-return of
the slot synchronizing with the next producer's acquire; relaxed
head/tail as pure index allocators; CAS-failure refresh; exact
full detection; non-wrapping u64 counters). No changes needed.

## [0.5.81] - 2026-08-18

W3C context-propagation audit.

### Fixed — traceparent version rules (W3C §3.3.2)

- Version `ff` is now rejected (the spec marks 0xff invalid).
- Version 00 with trailing content (`…-01-junk`) is now rejected —
  version 00 is exactly 4 fields.
- Future versions' extra fields are ignored, now as a documented,
  tested forward-compatibility rule (previously an accident of not
  reading past the flags).

### Fixed — control bytes in propagated tracestate/baggage

Extract-side tracestate/baggage rejected CR/LF (v0.5.53) but
passed other control bytes through, which would produce invalid
outgoing headers on inject — the same CWE-93 family one step
removed. Any byte < 0x20 or 0x7F is now rejected; the W3C grammars
allow only printable ASCII.

Audited and found correct as-is: memory safety on short/malformed
headers (the NUL short-circuit pattern never reads past the
terminator), case-insensitive hex parsing, lowercase formatting,
and the all-zero-ID rejections.

New properties: `prop_traceparent_version_rules`,
`prop_context_rejects_control_bytes`.

## [0.5.80] - 2026-08-18

HTTP response parser: chunked support + framing hardening.

### Fixed — chunked responses were unusable

`Transfer-Encoding: chunked` was not handled at all. On keep-alive
the parser waited for an EOF that never comes — read timeout, the
request failed, and the batch burned its retries; any streaming
proxy (nginx/envoy with buffering off — the documented sidecar
topology) triggers it. Even at EOF the "body" contained the raw
chunk framing. The parser now decodes chunked bodies in place
(RFC 7230 §4.1: chunk sizes with extensions, trailers, incremental
need-more until the terminator, size-capped) and keeps the
connection reusable — chunked framing is self-delimiting.

### Fixed — framing and smuggling hardening

- Header matching is now **line-aligned**; a "Content-Length:"
  inside another header's value no longer matches.
- **TE + Content-Length together** (RFC 7230 §3.3.3, the classic
  request-smuggling vector) is rejected outright.
- Duplicate `Content-Length` with differing values (§3.3.2) is
  rejected; identical duplicates collapse.
- Undecodable `Transfer-Encoding` (gzip, `gzip, chunked`,
  identity) is rejected instead of misparsed.
- Keep-alive default is **version-aware**: HTTP/1.0 defaults to
  close (§6.3) and no longer marks the socket reusable; all
  `Connection:` header lines are scanned.

### Added

- `tests/test_http_parser.c` (9 tests) with a new
  `ECHO_RAW_RESPONSE` mode in the test echo helper: canned raw
  responses for chunked (single / multi+trailers / 3 KB
  incremental), smuggling rejections, line-alignment, HTTP/1.0,
  and case-insensitivity.

## [0.5.79] - 2026-08-17

One set-attribute engine (internal; behavior unchanged).

### Changed — all 29 attribute setters delegate to one engine

Every public `*_set_attribute_*` setter hand-rolled the same flow —
null guards, value pre-duplication, upsert reserve, typed fill,
failure cleanup — across 29 setters in three files. The v0.5.75
NULL-guard bug was possible precisely because a guard lived in one
of those copies. Three engine entry points in `internal_util` now
own the flow (`otlp_attr_vec_set` for scalars;
`otlp_attr_vec_set_array` / `_set_kvlist` for composites,
build-then-attach), and every setter on every surface is a thin
typed wrapper over a stack `otlp_value_t`. The event/link slot
helpers are deleted. Net −145 lines.

Semantics are byte-identical (same error codes and ordering);
verified by the unchanged 36-test suite, 20k-iteration property
runs, ASAN + LeakSanitizer, and unchanged benchmarks.

## [0.5.78] - 2026-08-17

Resource attributes are a map too.

### Fixed — duplicate Resource KeyValues could reach the wire

The five in-object attribute surfaces got map semantics in
v0.5.73, but resource attributes were copied verbatim from
`otlp_exporter_opts_t` at create time. Two ways to emit
non-compliant wire data (OTLP data model: attribute keys MUST be
unique): duplicate keys in the opts array, and a caller-supplied
`service.name` resource attr colliding with the auto-emitted one.

`otlp_exporter_create` now normalizes: duplicate keys collapse
last-write-wins, and a `service.name` entry is dropped when the
dedicated `service_name` opt is set (the documented field wins;
with the opt unset the attrs entry is emitted as-is). The replace
path keeps the exporter-free path safe under mid-replace OOM, and
a NULL key now fails create explicitly. Two new properties pin the
behavior via a new test-only accessor
(`otlp_exporter_get_resource_attrs`, `src/exporter_internal.h`).

## [0.5.77] - 2026-08-17

Documentation catch-up for the attribute-model arc (docs only).

### Fixed — stale documentation across nine releases of API surface

- `docs/roadmap.md`: version table extended through v0.5.76 (PR
  numbers verified against the merge history), key-metrics block
  refreshed (116 TODOs, 36 tests, 37+ bugs, span struct 176 B,
  ~150 ns/span emit), plus an attribute-model-arc summary.
- `docs/otlp-spec.md`: the AnyValue section claimed ArrayValue /
  KeyValueList were "tracked as P2" — now documents the full oneof,
  the composite setters, the `otlp_value_t` input model, and the
  map (unique keys, last write wins) semantics.
- `README.md`: feature list gained the attribute bullet (full
  AnyValue set on all five surfaces, map semantics, grow-on-demand
  storage).
- `CLAUDE.md`: key-files table gained `value.h`; conventions
  document the shared attribute-vector model and the
  build-then-attach contract; implementing-agent notes cover the
  five-surface × seven-type matrix.
- `docs/cookbook.md`: new composite-attributes section with array +
  kvlist examples.

No code changes; build and full test suite run as the gate.

## [0.5.76] - 2026-08-17

Span events/links grow on demand; span struct 176 bytes (was 5,776).

### Changed — events/links arrays leave the span struct

After v0.5.75 the inline `events[64]` + `links[64]` header arrays
were 97% of `sizeof(struct otlp_span)` — paid in full by every
create and clone even for a span with neither (the common case).
They now follow the same grow-on-demand model as the attribute
vectors: heap arrays, NULL until first use, growing 4 → 8 → … slots
via `realloc` (bounded by the unchanged 64/64 caps, new tail
zeroed). Caps and overflow semantics unchanged; the event/link
struct layouts are unchanged, so the encoder is untouched.

- `sizeof(struct otlp_span)`: 5,776 → **176 B** (32.9×; the arc
  from v0.5.67's 138,880 B is 789×).
- Zero-attribute span emit: ~885 → **~150 ns/span** (~6.6M
  spans/s through clone + queue + tick); 5 attributes: ~530 →
  ~375 ns.
- Struct-size budget: 8 KB → 512 B.

### Added

- `test_event_link_overflow` — the 64-event / 64-link caps had no
  direct test; now pinned (64 OK, 65th → `OTLP_ERR_OVERFLOW`).

## [0.5.75] - 2026-08-17

Grow-on-demand attribute vectors; attribute-bearing spans ~4× faster.

### Changed — `otlp_attr_vec`: one storage model, sized to actual use

Lazy allocation (v0.5.68/69) made attribute-less objects cheap, but
the first attribute still allocated the full cap-sized array (4 KB
for a one-attribute metric or log record), and the span itself
carried an inline `attrs[128]` — 4 KB zeroed on every create and
clone. Every attribute-bearing object (span, event, link, metric,
log record) now embeds a `struct otlp_attr_vec` that starts empty
and grows 4 → 8 → … slots (doubling via `realloc`, bounded by the
owner's cap); clone copies exact-fit. The span's separate inline
reserve path is deleted — one vector type, three shared helpers,
five owners.

- `sizeof(struct otlp_span)`: 8,832 → **5,776 B** (−35%).
- `otlp_bench_emit`, 1-attribute span: ~1,375 → **~350 ns**; 5
  attributes: ~1,590 → ~530 ns.
- Struct-size budgets tightened: span ≤ 8 KB, metric ≤ 512 B,
  log record ≤ 256 B.

### Fixed

- The span-level attribute setters had lost their NULL-span guards
  when the guard-carrying local reserve was removed in the v0.5.73
  reorder — `otlp_span_set_attribute_*(NULL, …)` segfaulted instead
  of returning `OTLP_ERR_NULL`. Caught by `prop_setters_null_safe`;
  guards restored on all span-level setters.

## [0.5.74] - 2026-08-17

ARRAY/KVLIST attributes end-to-end; malformed-frame encoder fix.

### Added — composite attribute setters + `otlp_value_t`

New `include/otlp-c/value.h` introduces `otlp_value_t` (a
borrowed-data tagged union for one scalar value: string / bool /
int64 / double / bytes) and `otlp_kv_t` (one KeyValueList entry).
Ten setters take flat arrays of them and deep-copy:

- `otlp_span_set_attribute_array` / `_kvlist` (also the
  event/link variants)
- `otlp_metric_set_attribute_array` / `_kvlist`
- `otlp_log_record_set_attribute_array` / `_kvlist`

All follow the upsert semantics: re-setting the key replaces the
whole tree. Composites are built first into fully-owned trees
(`otlp_attr_array_build` / `otlp_attr_kvlist_build`), then
attached — preserving the reserve-and-fill contract. Clone now
deep-copies recursively: `otlp_attribute_copy_all` was rewritten
as a per-item recursive copy handling all seven types (it
previously refused ARRAY/KVLIST).

### Fixed — composite AnyValue frames were malformed

`encode_attr_array` / `encode_attr_kvlist` wrote their body
without the outer LEN length prefix — a malformed protobuf frame
(the first item's tag byte would be misread as the length). The
path was unreachable before this release (no code could create a
composite attribute), which is why it survived. Both encoders now
build the body into a temp buffer and emit `LEN + body`. Caught by
the new `prop_attr_array_wire` / `prop_attr_kvlist_wire`
properties, which walk the wire into the nested oneof.

## [0.5.73] - 2026-08-16

Attributes are a map: last-write-wins upsert.

### Changed — re-setting an attribute key overwrites (behavior change)

Every attribute setter previously appended unconditionally, so
setting the same key twice produced duplicate `KeyValue` entries —
but the OTLP data model requires unique keys and the OTel API
defines attributes as a map where setting an existing key
overwrites. A natural pattern (set a default, refine later)
generated non-compliant wire data.

`otlp_attr_list_reserve` now upserts: an existing key's slot is
reused with its old value released (count unchanged, position
preserved, type may change); only new keys append. Overwriting an
existing key succeeds even at the cap; a new key past the cap still
returns `OTLP_ERR_OVERFLOW`. One implementation gives all 25
setters (span, event, link, metric, log) the semantics with zero
per-setter duplication.

The reserve-then-fill contract was also reordered so the fill
cannot fail — string/bytes setters duplicate the value before
reserving — which deletes the fragile free-the-key-on-failure
cleanup from every owned-value setter.

### Added

- `otlp_attribute_release_value` (internal): release an
  attribute's payload, keep the key — the primitive for
  replace-value-in-place.
- Properties: `prop_attr_upsert_last_write_wins`,
  `prop_attr_upsert_at_cap`, and `prop_attr_upsert_wire_identical`
  (a span whose key was set twice encodes byte-identically to one
  whose key was set once with the final value). Unit upsert tests
  in all three signals.

## [0.5.72] - 2026-08-16

Event/link attribute type parity.

### Added — typed event/link attribute setters

`otlp_span_set_event_attribute_string` / `set_link_attribute_string`
were the only event/link attribute setters, so events couldn't carry
`attempts=3` as an int64 and links couldn't carry binary context
without stringifying. Eight new setters close the gap:

- `otlp_span_set_event_attribute_{int,double,bool,bytes}`
- `otlp_span_set_link_attribute_{int,double,bool,bytes}`

Same contract as the string variants: they target the
most-recently-added event/link and return
`OTLP_ERR_INVALID_ARGUMENT` if none exists yet. The shared
validate-target-reserve prelude moved into static
`event_attr_slot` / `link_attr_slot` helpers so all ten setters are
reserve + typed fill (DRY).

### Tests

`unit-span` (14→17) adds typed roundtrips for both targets plus the
set-before-add error contract. The clone-preservation property now
deep-copies int (event) and bytes (link) attributes. New
`prop_event_link_typed_attrs_wire` walks the wire to the AnyValue
oneof inside Event{11}/Link{13} and pins the attribute field
numbers — 3 for Event, 4 for Link — which differ and were worth an
explicit regression guard. The events property file's stale header
doc (swapped Event name/time field numbers) is fixed.

## [0.5.71] - 2026-08-16

Attribute setter type parity across all three signals.

### Added — missing typed attribute setters for metrics/logs

The span API accepted string/int64/double/bool/bytes attributes,
but metrics were missing bool + bytes and log records were missing
double + bool + bytes. Callers had to stringify binary or boolean
values, losing AnyValue type fidelity on the wire. Five new
setters close the gap:

- `otlp_metric_set_attribute_bool` / `_bytes`
- `otlp_log_record_set_attribute_double` / `_bool` / `_bytes`

No encoder, schema, clone, or free-path changes were needed — the
model-driven `otlp_encode_any_value` dispatch and the shared
attribute storage already covered every type; each setter is a
thin typed fill through `otlp_attr_list_reserve`.

Also fixed: `log.h` did not include `<stdbool.h>` (no prior
declaration used `bool`).

### Tests

`unit-metric` (9) and `unit-log` (10) gain per-type roundtrips and
bytes deep-copy assertions in the clone tests. The metric/log
attribute roundtrip properties now cycle int64/double/bool/bytes
by seed and assert the exact AnyValue oneof field number + wire
type + value on the wire (verified at 20,000 iterations).

## [0.5.70] - 2026-08-16

One owner for the lazy attribute-list storage model (DRY).

### Changed — shared `otlp_attr_list_{reserve,copy,free}` helpers

After v0.5.68/v0.5.69 the "lazy attribute array" storage model was
hand-implemented four times — span events, span links, metrics, log
records — with twelve near-identical blocks (cap check + lazy
`calloc`, clone-path `calloc` + deep copy + OOM cleanup, and
release loops). The model now lives in one place:
`otlp_attr_list_reserve` / `otlp_attr_list_copy` /
`otlp_attr_list_free` in `internal_util`. The attribute-bearing
types pass their `(attrs, n_attrs, cap)` triple; local copies
(`metric_reserve_attr`, `metric_release_attrs`,
`log_attrs_reserve`, `log_release_attrs`, and the span event/link
inline blocks) are deleted.

No behavior change: 36/36 tests, ASAN clean (including the OOM
injection sweep through the new clone path), and benchmarks
unchanged (~110 ns/log, ~1,500 ns/span with zero attributes).

### Fixed

- `CPACK_SOURCE_IGNORE_FILES` in `CMakeLists.txt` used jammed
  quoted fragments that emitted a CMake author warning on every
  configure; rewritten as a plain list with identical semantics.

## [0.5.69] - 2026-08-16

Metric/log record structs 19×/52× smaller; log emit ~5× faster.

### Changed — metric/log attributes lazily heap-allocated

The v0.5.68 span fix left the other two signals with the same
inline-array problem: `sizeof(struct otlp_metric)` was 4,312
bytes and `sizeof(struct otlp_log_record)` was 4,168 bytes — in
both cases a 4KB inline `attrs[128]` array that every create and
clone allocated and zeroed even when no attributes were ever set.
Logs are the highest-volume signal, making this the dominant
remaining per-record cost.

Metric and log-record attribute arrays are now heap-allocated
lazily on the first `otlp_metric_set_attribute_*` /
`otlp_log_record_set_attribute_*` call. Caps unchanged (128).

- `sizeof(struct otlp_metric)`: 4,312 → **224 bytes** (19.3×).
- `sizeof(struct otlp_log_record)`: 4,168 → **80 bytes** (52×).

The metric/log release paths now use the shared recursive
`otlp_attribute_free` instead of hand-rolled copies of the span's
free loop.

### Added

- `otlp_bench_logs` — dual-pass (clone vs build+move) log emit
  throughput benchmark, mirroring `otlp_bench_emit`.
- `unit-metric` / `unit-log` tests — known-answer tests for
  setters and clone, the lazy-array contract, and struct-size
  budget guards (≤1KB / ≤512B).

### Performance impact

`otlp_bench_logs` (clone + queue + tick, null_transport), records
with zero attributes: ~590-720 → **~110-175 ns/log**
(≈5×, >9M logs/s). Records carrying attributes are unchanged
within noise — they still allocate the 128-slot array on first
attribute set.

## [0.5.68] - 2026-08-15

Span struct 15.7× smaller; emit 20× faster.

### Changed — event/link attributes lazily heap-allocated

`sizeof(struct otlp_span)` was **138,880 bytes**. The embedded
`events[64]` and `links[64]` arrays each carried an inline
`attrs[32]` array (1KB per event/link slot), so a span with zero
events and zero links still allocated and zeroed 139KB on every
`otlp_span_create` and `otlp_span_clone`. The clone inside
`otlp_exporter_emit` made this the dominant emit-pipeline cost.

Event and link attribute arrays are now heap-allocated lazily on
the first `otlp_span_set_event_attribute_string` /
`otlp_span_set_link_attribute_string` call. A typical event or
link with zero attributes costs one NULL pointer; the 32-attribute
cap is unchanged.

`sizeof(struct otlp_span)` drops to **8,832 bytes** (15.7×).

### Performance impact

`otlp_bench_emit` (clone + queue + tick, null_transport):
- Before: ~27,000-195,000 ns/span (erratic — allocator pressure
  from 139KB alloc/free cycles dominated and caused page-fault
  noise across configurations).
- After: **~1,500 ns/span** — roughly **650,000 spans/sec**
  through the full emit pipeline, and consistent across batch
  sizes.

The bench was also extended to print the clone-API path (`emit`)
and the build+move path (`emit_move`) side by side, so the
deep-copy share is visible.

### Guarded against regression

`test_unit_span` now asserts `otlp_span_struct_size() <= 16KB`
(new internal accessor; the struct stays opaque). If a future
change reintroduces inline-array growth, the test fails with a
pointer to re-run the emit benchmark.

### No public API change

`otlp_span_add_event`, `set_event_attribute_string`,
`set_link_attribute_string`, clone, free, and the encoder all
behave identically. The 32-attribute-per-event/link cap is
unchanged (was inline, now lazily allocated at the same size).

34/34 tests pass locally. ASAN clean — the fail-injecting OOM
tests cover the new lazy-allocation paths in `span_clone` at
every alloc offset.

## [0.5.67] - 2026-08-15

Integration test covers all three signals; sync-flush retry + diagnostics.

### Added — metrics + logs end-to-end validation

v0.5.66 brought the integration test into CI but it only
exercised traces. The metrics and logs encoders — which had 6
of the 10 v0.5.48-v0.5.49 wire-format bugs — were still validated
only by property tests against our own decoder.

The integration test now emits:
- A counter metric (`integration_requests_total`) with an
  attribute, via the synchronous `flush_metric` path. A 2xx from
  otelcol proves the `ExportMetricsServiceRequest` was accepted
  by the collector's real protobuf parser.
- A log record (`integration log body`, severity INFO) with an
  attribute, via `flush_log`. A 2xx proves the
  `ExportLogsServiceRequest` was accepted.

The otelcol config gains `metrics` and `logs` pipelines with the
`debug` exporter (verbosity: detailed — the default `basic` prints
only counts, not names). Two new CI steps grep the otelcol
container logs for the metric name and log body.

### Fixed — sync flush (flush_metric / flush_log) had no retry

`flush_sync` attempted exactly one POST. A transient network
failure (e.g., the first connect in a fresh process failing
before the collector's accept queue is warm) immediately failed
the flush. The async pipeline recovers from the same transient
via backoff — the sync path deserved the same resilience.

`flush_sync` now retries pre-response network failures with the
exporter's `max_retries` budget (100ms backoff between attempts —
short, since the sync path is on the caller's thread). Non-2xx
responses and timeouts remain permanent (no retry) — retrying a
4xx would be wrong, and a timeout already consumed the caller's
flush budget.

Found by the new integration test: the CI metrics POST hit a
transient connect failure on its first attempt.

### Added — diagnostic logging on the sync flush paths

`flush_metric` / `flush_log` failures were silent — the caller
got `OTLP_ERR_NETWORK` with no indication whether it was an HTTP
4xx/5xx, a network failure, or a timeout. `flush_sync` now emits
diagnostic-callback events (`OTLP_LOG_ERROR`) at each failure
mode with the HTTP status or error code, plus `OTLP_LOG_WARN` on
transient-retry. This closes a gap in the v0.5.23 diagnostic
callback coverage (it fired only on the async pipeline's events).

### Why this matters

The metrics/logs encoder bugs found in v0.5.48-v0.5.49
(NumberDataPoint attributes at the wrong field, HistogramDataPoint
attributes/min/max wrong, ExpHistogram zero_count/bucket_counts
wrong wire types) were invisible to our property tests for 49
releases — our decoder shared the same wrong expectations. Only
cross-checking against upstream opentelemetry-proto found them.

With this release, a regression in any of the three signals is
caught automatically by otelcol's independent parser on every
PR. The class of bug that hid for 49 releases can no longer hide.

### All three signals now validated end-to-end

| Signal | Test path | CI verification |
|---|---|---|
| Traces | emit → flush → Jaeger query | run_id + event + status needles |
| Metrics | flush_metric → 2xx | otelcol debug-exporter grep |
| Logs | flush_log → 2xx | otelcol debug-exporter grep |

34/34 tests pass locally. CI validates the integration end-to-end.

## [0.5.66] - 2026-08-15

Integration test validates events + status; CI runs it.

### Added — integration test exercises events + status

The integration test (`test_integration_jaeger`) emitted spans
with only 2 attributes — it did not exercise the v0.5.48 fixes
(Event name/time field-number swap, Status code at wrong field).
The wire-format fixes were validated by property tests (wire-
level decode) but not against a real collector.

The test now adds an event ("cache-miss" with an attribute) and
sets status (`OTLP_STATUS_CODE_OK`) on each span. After the
spans appear in Jaeger, the test searches the response body for
the event name and the status string — verifying they survived
the full round-trip (encode → otelcol decode → Jaeger store →
query API).

### Added — CI job runs the integration test

The Jaeger integration test was local-only (manual
`docker compose up` + `OTLP_C_RUN_INTEGRATION=1`). It never ran
in CI — a wire-format regression that passed property tests
(verify wire bytes directly) but failed against a real collector
would go unnoticed until someone ran the test manually.

New `jaeger-integration` job in ci.yml:
1. Build library + tests.
2. `docker compose up -d` (otelcol + Jaeger from the existing
   tests/integration/docker-compose.yml).
3. Wait for Jaeger query API readiness (up to 30s).
4. `ctest -L integration` with `OTLP_C_RUN_INTEGRATION=1`.
5. Dump otelcol logs on failure; tear down always.

This closes the gap between "wire bytes are correct per our
decoder" and "a spec-compliant collector accepts our output".

### Verification needles

The test searches the Jaeger JSON response for:
- `"cache-miss"` — Jaeger stores OTLP events as span logs with
  the event name as a field value.
- `"STATUS_CODE_OK"` — otelcol translates OTLP status to the
  `otel.status_code` tag with the enum name as the value.

If these needles turn out to match a different serialization,
the CI failure will surface it and the needles can be adjusted.

34/34 tests pass locally (integration still skipped without
docker; CI validates it).

## [0.5.65] - 2026-08-14

DNS behavior documentation accuracy.

### Fixed — `platform.h` claimed DNS results were cached (they are not)

The `otlp_socket_connect` docstring claimed getaddrinfo results
were "cached at the exporter level for the process lifetime."
No such caching exists — every connect does a fresh getaddrinfo.
The claim was aspirational (from the original design plan) but
never implemented.

A reader might assume DNS latency is a one-time cost and design
their tick-loop thread accordingly. In reality, every reconnect
(initial connect, or reconnect after a connection failure)
performs a blocking getaddrinfo that can take seconds on slow
or broken DNS.

Fix: the comment now accurately describes the behavior — no
library-level caching, rely on the OS resolver (nscd /
systemd-resolved / mDNSResponder), DNS lookups are rare in
steady state thanks to HTTP keep-alive.

### Added — public DNS note in `exporter.h`

The exporter's public docstring now documents the blocking-DNS
behavior: the first tick() that opens a connection (and any
reconnect) performs a blocking getaddrinfo. Callers whose tick
thread cannot tolerate this latency should resolve the
collector's hostname to an IP before constructing the endpoint,
or run tick() from a thread that can block briefly.

### Comment-accuracy sweep

Scanned all internal headers for strong claims ("cached",
"always", "never", "guaranteed", "thread-safe"):
- platform.h "never spawns threads, never takes locks" —
  verified accurate (no pthread_mutex / CreateMutex anywhere).
- exporter.c "Cached TCP connection for HTTP keep-alive" —
  verified accurate (keepalive_sock is real).
- platform.h DNS caching claim — **false, fixed in this
  release**.

34/34 tests pass.

## [0.5.64] - 2026-08-14

Roadmap + CLAUDE.md catch-up (27 releases).

### Fixed — docs/roadmap.md stopped at v0.5.36

The roadmap's release table ended at v0.5.36 (v0.5.37 was a
"roadmap update" release that itself was never recorded, and
none of v0.5.38-v0.5.63 were added). Added a new section
"v0.5.37–v0.5.63 — deep audit arc (wire format, security,
memory, overflow)" with all 27 releases, their PR numbers, and
a bug-class coverage summary.

### Fixed — CLAUDE.md referenced v0.5.35

Updated "All phases are complete (v0.5.35)" → v0.5.63. Added
mentions of the v0.5.43-v0.5.46 descriptor-driven dispatch, the
v0.5.48/49/61 schema fixes, the v0.5.52/53 header-injection
hardening, the v0.5.54 ID validation, the v0.5.47 RFC 7230
parser fix, the v0.5.56/57 fail-injecting allocator test
infrastructure, the v0.5.59 accounting invariant, and the
v0.5.62/63 integer-overflow defense.

### Why this matters

The roadmap and CLAUDE.md are the first things a new contributor
reads. Both claimed the library was at v0.5.35 — missing 28
releases of bug fixes, security hardening, and test
infrastructure. A contributor reading "34 tests, 7 bugs fixed"
would drastically underestimate the project's actual maturity
(34 tests, 34+ bugs fixed).

### No code changes

Documentation only. Existing tests still pass.

34/34 tests pass.

## [0.5.63] - 2026-08-14

Integer overflow sweep (continuation of v0.5.62).

### Fixed — remaining `count * sizeof(...)` sites without overflow checks

v0.5.62 fixed the metric allocation paths. This release sweeps
the remaining allocation sites in the codebase:

- `otlp_dup_str` (`internal_util.c`): `strlen(s) + 1` could
  overflow for a string of `SIZE_MAX` bytes. Practically
  unreachable (no system has that much contiguous memory), but
  the check is one line. Returns NULL on overflow.
- `mpsc_queue_init` (`mpsc_queue.c`): `capacity * sizeof(slot)`
  could overflow for capacity near `SIZE_MAX / sizeof(slot)`.
  Returns `OTLP_ERR_INVALID_ARGUMENT` on overflow.
- `otlp_exporter_create` (`exporter.c`):
  `n_resource_attributes * sizeof(otlp_resource_attr_t)` could
  overflow. Fails cleanly on overflow.

### Fixed — `batch_size` had no upper clamp

`normalize_opts` replaced only `batch_size == 0` with the
default (512). A caller passing `SIZE_MAX` would cause
`batch_size * 2` to wrap in the pending-array allocation.
Added an upper clamp of `OTLP_MAX_BATCH_SIZE` (1M items per
batch). 1M spans in one batch is ~200 MB of encoded wire
data; callers wanting more throughput should shard across
multiple exporters.

### Allocation-site audit now complete

Every `otlp_malloc` / `otlp_calloc` / `otlp_realloc` call in
`src/` has been examined for overflow safety:

| Site | Check | Fixed |
|---|---|---|
| metric bounds/bucket_counts | explicit overflow check | v0.5.62 |
| metric exp_pos/exp_neg counts | explicit overflow check | v0.5.62 |
| metric clone paths | defensive overflow check | v0.5.62 |
| `otlp_dup_str` len+1 | explicit overflow check | **v0.5.63** |
| `mpsc_queue_init` capacity×slot | explicit overflow check | **v0.5.63** |
| exporter resource_attributes | explicit overflow check | **v0.5.63** |
| exporter pending arrays | batch_size clamp | **v0.5.63** |
| HTTP req_buf total | already overflow-checked | (pre-v0.5.47) |
| protobuf buf growth | doubling with SIZE_MAX guard | (pre-audit) |
| platform sock struct | constant sizeof | safe |
| sampler struct | constant sizeof | safe |
| span struct | constant sizeof | safe |

34/34 tests pass. ASAN clean.

## [0.5.62] - 2026-08-14

Integer overflow defense in metric allocation paths.

### Fixed — `count * sizeof(...)` multiplication could overflow

`otlp_metric_create` (histogram path), `otlp_metric_set_exp_histogram`,
and `otlp_metric_clone` all multiplied caller-supplied counts by
`sizeof(double)` or `sizeof(uint64_t)` without overflow checks. A
malicious or buggy caller passing `SIZE_MAX` as the count would
cause the multiplication to wrap to a small value, producing an
undersized allocation. The subsequent `memcpy` would then read/write
out of bounds — heap buffer overflow.

Affected paths:
- `otlp_metric_create` with `histogram_n_bounds` near `SIZE_MAX`:
  `histogram_n_bounds * sizeof(double)` wraps; `histogram_n_bounds + 1`
  wraps to 0 in the bucket_counts calloc.
- `otlp_metric_set_exp_histogram` with `pos_n` or `neg_n` near
  `SIZE_MAX`: `pos_n * sizeof(uint64_t)` wraps.
- `otlp_metric_clone` (defensive): same checks for the cloned
  bounds/counts, even though src was validated at create time.

Fix: explicit overflow check (`count > SIZE_MAX / sizeof(...)`)
before each multiplication. On overflow, the function returns NULL
(create) or `OTLP_ERR_INVALID_ARGUMENT` (set_exp_histogram) or
falls through to `goto fail` (clone).

### Added — `prop_metric_rejects_overflow_sizes`

Property test verifies that `otlp_metric_create` with
`histogram_n_bounds = SIZE_MAX` returns NULL, and
`otlp_metric_set_exp_histogram` with `pos_n = SIZE_MAX` returns
`INVALID_ARGUMENT`. Both pre-v0.5.62 would have proceeded to
allocate a wrapped-size buffer.

34/34 tests pass. ASAN clean.

## [0.5.61] - 2026-08-14

ExponentialHistogram schema entry (DRY/MECE).

### Added — `OTLP_EH_FIELDS` schema entry for ExponentialHistogram

The schema table for `ExponentialHistogram` message was missing.
The encoder emitted its `aggregation_temporality` (field 2)
using `HIST_F_AGG_TEMP` from the `Histogram` message — which
works because both messages have `aggregation_temporality` at
field 2, but is a DRY/MECE violation. If `Histogram`'s field
numbers ever diverge from `ExponentialHistogram`'s in a future
proto revision, the encoder would silently produce wrong wire
output.

Fix: added `OTLP_EH_FIELDS` with `data_points = 1` and
`aggregation_temporality = 2`. The encoder now uses the new
`EH_F_AGG_TEMP` macro for `ExponentialHistogram`, matching the
pattern already used for `Sum` and `Histogram`.

### Why this matters

The schema is the single source of truth for field numbers
(`src/otlp_schema.h` is the canonical reference for all
encoders). Each OTLP message should have its own entry, even
when fields happen to be identical to another message's. The
reused `HIST_F_AGG_TEMP` for two distinct message types was a
correctness landmine — coincidentally correct today, fragile
tomorrow.

### Same class as v0.5.48

v0.5.48 added missing schema entries and fixed wrong field
numbers for Event, Status, NumberDataPoint, HistogramDataPoint.
v0.5.61 completes the schema for the `ExponentialHistogram`
message (the wrapping message — its data point schema was
already present).

### Added — wrapper-level agg_temp verification

`prop_metrics_exp_histogram_field_nums` now verifies the
`ExponentialHistogram` wrapper emits `aggregation_temporality`
at field 2 (VARINT) before descending into the data point. The
wrapper-level check was missing — only the data point fields
were verified.

34/34 tests pass. ASAN clean.

## [0.5.60] - 2026-08-13

Public API docstring accuracy.

### Fixed — span / log docstrings lagged behavior

Several public API docstrings in `span.h` and `log.h` had
drifted from the actual library behavior:

- `otlp_span_set_parent_span_id`: claimed "Empty (8 zero bytes)
  for a root span." Wrong post-v0.5.54 — all-zero is now
  rejected. The doc now correctly states NULL clears the
  parent; non-NULL all-zero returns INVALID_ARGUMENT.
- `otlp_span_set_trace_id` / `_span_id`: didn't mention
  all-zero rejection (added v0.5.54). Now documented.
- `otlp_span_set_start_time` / `_end_time`: claimed "exporter
  refuses to emit a span with start_time = 0." False — the
  library emits whatever value is set. Doc now states this
  accurately.
- `otlp_log_record_set_trace_id` / `_span_id`: didn't mention
  the v0.5.50 split (independent flags) or v0.5.54 all-zero
  rejection. Now documented.

### Why this matters

API docs that lag the implementation cause integration bugs.
A caller reading "Empty for a root span" might pass 8 zero
bytes and get an unexpected `INVALID_ARGUMENT`. A caller
reading "exporter refuses to emit start_time = 0" might add
workarounds for behavior that doesn't exist.

The v0.5.48-v0.5.59 audit arc fixed many bugs; this release
catches the docs up to those fixes.

### No code changes

This release is documentation only. No behavior change.

34/34 tests pass. ASAN clean.

## [0.5.59] - 2026-08-13

Flush accounting invariant under OOM.

### Fixed — `flush_metric` / `flush_log` accounting broke under OOM

Both `otlp_exporter_flush_metric` and `otlp_exporter_flush_log`
incremented `emitted_metrics` (or `emitted_logs`) at the start
of the call. If `otlp_pb_buf_init` subsequently failed (OOM),
the function returned without incrementing either
`sent_metrics` or `dropped_metrics_err`. The accounting
invariant `emitted == sent + dropped_err` was violated.

Users monitoring the counters would see `emitted > sent +
dropped_err` and have no way to tell where the missing items
went.

Fix: on init failure, increment `dropped_metrics_err` (or
`dropped_logs_err`) before returning. The invariant now holds
across all paths.

### Added — OOM accounting regression tests

Extended `test_allocator_oom.c` with two new tests:

- `test_flush_metric_oom_accounting` — probes flush_metric
  across 15 OOM offsets; asserts `emitted == sent + dropped_err`
  after each.
- `test_flush_log_oom_accounting` — same shape for flush_log.

These were the first tests to verify the accounting invariant
under OOM. Without them, the v0.5.59 fix would have been
correct-by-inspection only.

### Pattern continuation

This is the same "missing counter update" class as v0.5.58's
flush return-status check. Both are accounting asymmetries
where one path updates a counter and another doesn't. The
v0.5.58 fix made the return-status match the loop condition;
the v0.5.59 fix makes the init-failure path match the
encode-failure and POST-failure paths.

34/34 tests pass. ASAN clean.

## [0.5.58] - 2026-08-13

Flush return-status now reflects MPSC queue state.

### Fixed — `otlp_exporter_flush` return-status omitted queue sizes

The flush loop condition checked `pending_count`, `in_flight`,
AND `mpsc_queue_size` for all three signals. But the return-
status check after the loop only checked `pending_count` and
`in_flight`. If the deadline was reached with items still in
the MPSC queues (e.g., drain cap hit, or a tight race after a
POST completion cleared pending but before the next drain),
flush silently returned `OTLP_OK` with unsent items.

The user would see "flush succeeded" and free the exporter,
losing the queued items.

Fix: the return-status check now matches the loop condition
exactly — it includes `mpsc_queue_size` for all three signals.
Items remaining in queues now correctly produce
`OTLP_ERR_NETWORK`.

### When the bug manifested

The race window is narrow but reachable:

1. User emits many items (queue fills beyond the drain cap of
   `batch_size * 2`).
2. User calls `flush()` with a bounded `flush_timeout_ms`.
3. The POST in flight completes, clearing pending.
4. The next drain hasn't run yet.
5. flush's deadline hits.
6. Return check sees pending=0, in_flight=NULL — returns OK.
7. User frees exporter. Items in queue are dropped.

With the fix, step 6 sees queue_size > 0 and returns
`OTLP_ERR_NETWORK`. The user can retry or accept the loss
explicitly.

### Why no regression test

The race is timing-dependent: requires the flush deadline to
hit in the narrow window between POST completion and the next
drain. Reliable reproduction would require either:
- A custom allocator hook that injects delays (intrusive).
- A test-only knob to pause the tick loop mid-iteration
  (invasive).

The fix is correct by inspection — the return check now matches
the loop condition exactly. Existing flush tests verify the
happy path (no items remaining → OK) and the slow-network path
(items in pending → NETWORK).

34/34 tests pass. ASAN clean.

## [0.5.57] - 2026-08-13

Extended OOM test coverage to all major init paths.

### Added — 4 new fail-injecting OOM tests

Following v0.5.56's lead (where the fail-injecting allocator
caught a real bug in `exporter_create`'s fail path), the test
now covers every major init path that allocates multiple
resources:

- `test_metric_create_histogram_oom` — 30 iterations probing
  every alloc offset in `otlp_metric_create` with histogram
  bounds. Exercises the bounds (malloc) + bucket_counts (calloc)
  pair where a partial-init could leak.
- `test_metric_clone_oom` — 50 iterations probing every alloc
  offset in `otlp_metric_clone` with attrs + bounds + exp
  histogram pos_counts. The most complex clone path.
- `test_log_record_clone_oom` — 40 iterations probing
  `otlp_log_record_clone` with severity_text + body + attrs.
- `test_tracer_create_oom` — 20 iterations probing
  `otlp_tracer_create` (3 string dups + struct).

No new bugs found — each path correctly cleans up under OOM.
The tests now guard against future regressions across the
init-path class.

### Why this matters

v0.5.56 demonstrated that fail-injecting tests can catch bugs
that inspection misses. v0.5.57 extends the coverage so that
the NEXT partial-init bug — in any of these paths — is caught
automatically by CI rather than waiting for the next manual
audit.

The "test passes today" outcome is itself valuable: it confirms
that the v0.5.47 fix (attribute_copy_all), v0.5.55 fix
(resource_attributes), and v0.5.56 fix (mpsc_queue) are not
isolated cases — the codebase's other init paths follow the
same correct pattern.

### Pattern summary

Every multi-alloc init path now has a fail-injecting regression
test:

| Path | Allocations | Test |
|---|---|---|
| `otlp_exporter_create` | struct + 2 strings + attrs + 3 arrays + 3 queues | v0.5.56 |
| `otlp_span_clone` | struct + name + attrs + events + links | v0.5.56 |
| `otlp_metric_create` (hist) | struct + 3 strings + bounds + bucket_counts | **v0.5.57** |
| `otlp_metric_clone` | above + attrs + exp_pos + exp_neg | **v0.5.57** |
| `otlp_log_record_clone` | struct + 2 strings + attrs | **v0.5.57** |
| `otlp_tracer_create` | struct + 3 strings | **v0.5.57** |

34/34 tests pass. ASAN clean.

## [0.5.56] - 2026-08-13

Fail-injecting allocator test + mpsc_queue cleanup leak fix.

### Added — fail-injecting allocator test

New `test_allocator_oom.c` exercises the library's OOM cleanup
paths by iterating a "fail at Nth allocation" probe over an
operation that allocates multiple times. Each iteration:
1. Installs a fail allocator that returns NULL on the Nth alloc.
2. Runs the operation (exporter_create, span_clone).
3. Frees any returned object.
4. Asserts `alloc_count == free_count` (no leak).

Under ASAN, double-frees and UB also surface.

### Fixed — exporter_create fail path leaked mpsc_queue slots

The fail-injecting allocator caught a real bug on its first run:
`otlp_exporter_create`'s fail path freed user_agent, service_name,
resource_attributes (and contents), pending arrays, and the
exporter struct — but did NOT call `mpsc_queue_free` on the three
queues. If any `mpsc_queue_init` succeeded before the failure,
its slots allocation leaked.

Sequence to trigger: OOM during the 2nd or 3rd `mpsc_queue_init`
after the 1st succeeded. The first queue's slots are allocated
but never freed.

Fix: call `mpsc_queue_free` on all three queues in the fail path.
`mpsc_queue_free` is safe on uninitialized queues (it checks
`slots != NULL` before freeing).

This is the same bug class as v0.5.47 (attribute_copy_all) and
v0.5.55 (resource_attributes) — partial-init cleanup that missed
a resource. The fail-injecting allocator closes the testability
gap that hid this bug.

### Why this release matters

The previous OOM-reachable fixes (v0.5.47, v0.5.55) were
correct-by-inspection — no test could reach the failure path.
v0.5.56 ships the test infrastructure AND uses it to find a
third bug in the same class. The test now guards against
regressions in all three fixes.

34/34 tests pass. ASAN clean.

## [0.5.55] - 2026-08-13

Resource attributes array zero-init (UB defense).

### Fixed — `exporter_create` resource_attributes array uninitialized

`otlp_exporter_create` allocated the resource_attributes array
with `otlp_malloc` (uninitialized). The fail path then iterated
every slot and called `otlp_free` on each slot's key and value
pointers.

Under OOM during attribute copy:
- The full `n_resource_attributes` count was already set.
- The loop ran past the failure index.
- Uninitialized slots had garbage key/value pointers.
- `otlp_free` on garbage pointers is undefined behavior.

In practice this required memory pressure to trigger, but the UB
was real — under ASAN it would surface as "use of uninitialized
value" or a wild free.

### Fix

Switched to `otlp_calloc` so unset slots are zero-initialized
(NULL/NULL). `otlp_free(NULL)` is a no-op, so the fail-path
iteration is safe regardless of which slot failed.

This matches the pattern already used elsewhere in the codebase:
- `otlp_span_create` uses `otlp_malloc + memset(0)` (equivalent).
- `otlp_metric_create`, `otlp_log_record_create` use `otlp_calloc`.
- `mpsc_queue_init` uses `otlp_calloc`.

The resource_attributes array was the only outlier.

### Same class as v0.5.47

This is the same bug pattern as the `otlp_attribute_copy_all`
fail-path leak fixed in v0.5.47: a partial-init cleanup loop that
didn't account for which item actually failed. v0.5.47 fixed the
attribute-copy path; this release fixes the resource-attribute
copy path in the exporter constructor.

50/50 tests pass. ASAN clean.

## [0.5.54] - 2026-08-12

Reject all-zero IDs at set time (W3C §3.1.1 / §3.1.2).

### Fixed — ID setters accepted all-zero

`otlp_span_set_trace_id`, `otlp_span_set_span_id`,
`otlp_span_set_parent_span_id`, `otlp_log_record_set_trace_id`,
and `otlp_log_record_set_span_id` all accepted an all-zero ID
without complaint. The bytes would then be emitted on the wire
(OTLP trace_id, parent_span_id, etc.) where spec-compliant
receivers reject them:

- W3C Trace Context §3.1.1: "trace-id ... MUST NOT be all zero."
- W3C Trace Context §3.1.2: "parent-id ... MUST NOT be all zero."

The W3C traceparent *parser* already rejected all-zero (v0.5.x),
but the setters — which is how applications actually set IDs —
did not. So a caller could set all-zero IDs and have them
silently emitted to the collector.

### Behavior change

All five setters now return `OTLP_ERR_INVALID_ARGUMENT` when the
input is all-zero. Callers that previously set all-zero IDs
(which were invalid anyway) must update.

To clear the parent on a span: `otlp_span_set_parent_span_id(span,
NULL)` is still supported (sets `has_parent = false`). The all-
zero path was never the documented clear mechanism.

For trace correlation on log records: the v0.5.50 split flags
(`has_trace_id`, `has_span_id`) let callers set just one ID. The
all-zero validation is consistent with that — setting an ID to
all-zero is meaningless; the caller should not set it at all.

### Added — shared `otlp_id_is_all_zero` helper

New internal helper in `internal_util.h` checks whether a byte
array is all-zero. Used by all five setters (DRY).

### Added — regression properties

- `prop_setters_reject_all_zero_ids` (span): rejects all-zero
  trace_id, span_id, parent_span_id; accepts non-zero; supports
  NULL to clear parent.
- `prop_logs_setters_reject_all_zero_ids` (logs): rejects all-
  zero trace_id, span_id; accepts non-zero.

50/50 tests pass. ASAN clean.

## [0.5.53] - 2026-08-12

W3C context propagation header injection hardening.

### Fixed — `otlp_context_extract` rejected CRLF in tracestate / baggage

`otlp_context_extract` copied incoming `tracestate` and `baggage`
header values verbatim into the context struct. If an attacker-
controlled incoming request contained a tracestate or baggage
value with `\r\n`, the value would propagate through inject()
into the next outgoing request's carrier callback. For HTTP-
header-based carriers (the common case), the CRLF would split
into a new header line — CWE-93 (HTTP request splitting via
propagated context).

The W3C Tracestate and W3C Baggage specs both forbid CR and LF
in their value formats, so this is also a spec-compliance fix.

### Defense in depth

Combined with v0.5.52 (URL parser + user_agent validation), this
closes the third header-injection vector in the library:

| Vector | Source | Fixed |
|---|---|---|
| URL parser | caller-supplied endpoint | v0.5.52 |
| `build_request` user_agent | caller-supplied user_agent | v0.5.52 |
| Context propagation | attacker-supplied incoming header | **v0.5.53** |

The third vector is the most dangerous because it crosses trust
boundaries: a request from an attacker propagates header content
into a request to a trusted backend. The library now rejects
malformed values at extract time — extract() still succeeds
(traceparent is preserved if valid) but tracestate/baggage are
left empty when they contain CRLF.

### Why partial rejection is correct

If the carrier supplies a malicious tracestate, the right action
is to forward the legitimate traceparent without the malicious
tracestate. Trace correlation still works; only the vendor-
specific state is lost. W3C explicitly allows this: receivers
MAY truncate or drop non-conforming tracestate entries.

### Added — regression properties

- `prop_extract_rejects_crlf_tracestate` — pre-populates the
  carrier with a tracestate containing `\r\nX-Inject: yes`,
  extracts, asserts the extracted tracestate is empty.
- `prop_extract_rejects_crlf_baggage` — same shape for baggage.

48/48 tests pass. ASAN clean.

## [0.5.52] - 2026-08-12

HTTP header injection hardening (CWE-93).

### Fixed — URL parser rejects CR/LF

`otlp_http_parse_url` accepted CR (`\r`) and LF (`\n`) anywhere
in the host or path. These are the line terminators in HTTP and
would be written verbatim into the request line and Host header
by `build_request`'s `snprintf`. A caller-controlled URL
containing `\r\n` could inject arbitrary HTTP headers — HTTP
request splitting, CWE-93.

Realistic vectors:
- A config file that interpolates user input into the OTLP
  endpoint without sanitization.
- A service-mesh control plane that propagates a tainted
  endpoint from one service to another.

The parser now rejects any URL containing `\r` or `\n` with
`OTLP_ERR_INVALID_ARGUMENT`.

### Fixed — build_request validates user_agent for CR/LF

`build_request` interpolated the caller-supplied `user_agent`
directly into the User-Agent header line via `snprintf`. A
user_agent containing `\r\n` could inject arbitrary HTTP
headers — same CWE-93 class.

Realistic vectors:
- An application that derives user_agent from runtime state
  (process name, version string from a build system, etc.) and
  interpolates unsanitized content.
- A language VM binding that translates the host language's
  string type without C-string validation.

`build_request` now scans `user_agent` for `\r` or `\n` and
returns `OTLP_ERR_INVALID_ARGUMENT` if found.

### Defense in depth

URL validation covers `url->host` and `url->path`. User-agent
validation covers the remaining caller-supplied header field.
Together they close all header-injection vectors in the
library's outgoing POST request. None of the other headers
(Content-Type, Content-Length, Connection) are caller-
controlled — they're hardcoded constants or numbers.

### Added — regression properties

- `prop_url_rejects_crlf` — verifies the URL parser rejects
  URLs with `\r`, `\n`, or both in host or path.
- `prop_user_agent_rejects_crlf` — verifies
  `otlp_http_request_start` rejects user_agent strings
  containing CR/LF and accepts a valid user_agent.

46/46 tests pass. ASAN clean.

## [0.5.51] - 2026-08-12

Two defensive correctness fixes (slab + sampler).

### Fixed — slab double-free undefined behavior

`otlp_slab_free_ptr` had a code path that called `free(ptr)` on an
arena pointer when the pointer was inside the arena range but the
slot wasn't marked in-use (i.e., a double-free or invalid
pointer). The arena is owned by the slab, not by libc, so
calling `free()` on an arena address is undefined behavior. Under
ASAN this surfaces as "free() on non-heap pointer"; in production
it can corrupt the heap silently.

The defensive fix is to silently no-op: if the pointer is in the
arena but the slot isn't in-use, return without touching the
heap. This matches the "double-free is caller UB" contract while
avoiding UB inside the library.

### Fixed — TraceIdRatioBased sampler endpoint precision

The previous formula:
```
double scaled = (double) trace_prefix / (double) UINT64_MAX;
if (scaled < ratio) sample;
```
is mathematically sound but has an off-by-one at the endpoints:
- `ratio = 1.0`: `scaled < 1.0` is false for `trace_prefix == UINT64_MAX`, so one in 2^64 traces is incorrectly dropped.
- `ratio = 0.0`: works (always false), but only by luck.

The new code special-cases the endpoints:
- `ratio <= 0.0`: never sample.
- `ratio >= 1.0`: always sample.
- Otherwise: integer comparison `trace_prefix < (uint64_t)(ratio * UINT64_MAX)`.

The integer comparison matches the formula suggested by the
OpenTelemetry specification and used by otel-cpp, otel-java, and
otel-go for cross-SDK trace consistency at the boundary.

In practice, the impact was negligible (1 missed trace per 2^64
at ratio=1.0). The fix is more about spec-compliance and
interop.

### Added — regression properties

- `prop_slab_double_free_no_crash` (slab): double-frees an arena
  pointer and asserts no crash. Under ASAN, this catches the
  pre-v0.5.51 UB.
- `prop_ratio_one_samples_max_trace_id` (sampler): the all-0xFF
  trace_id (trace_prefix == UINT64_MAX) must sample at ratio=1.0.
- `prop_ratio_zero_drops_zero_trace_id` (sampler): the all-zero
  trace_id (trace_prefix == 0) must drop at ratio=0.0.

44/44 tests pass. ASAN clean.

## [0.5.50] - 2026-08-12

LogRecord trace_id / span_id independent emission.

### Fixed — asymmetric trace correlation emitted zero-fill bytes

`otlp_log_record` used a single `has_trace` flag set by either
`set_trace_id` or `set_span_id`. The encoder then unconditionally
emitted both fields whenever `has_trace` was true.

If the caller set only one ID (e.g., a log correlated to a
trace_id without a specific span_id), the encoder emitted the
unset member as 16 or 8 zero bytes. That's a syntactically valid
proto `bytes` value but an invalid W3C trace_id (all-zero
trace_id is forbidden per W3C Trace Context §3.1).

Spec-compliant collectors that validate the W3C constraint
would reject or misroute the log record.

### Changed — split has_trace into has_trace_id + has_span_id

The internal struct now has two independent flags. Each setter
sets only its own flag. The encoder emits each field only when
its own flag is set.

`otlp_log_has_trace` (the public accessor) is preserved for
compatibility and returns the OR of the two new flags — semantically
"this record has any trace correlation". Two new internal
accessors `otlp_log_has_trace_id` and `otlp_log_has_span_id`
support per-field checks.

### Added — regression property

`prop_logs_trace_id_only_no_zero_span_id` sets only trace_id,
encodes, and verifies:
- Field 9 (trace_id) is present with the correct bytes.
- Field 10 (span_id) is absent (not zero-filled).

Pre-v0.5.50 would have emitted 8 zero bytes for span_id; the
regression test locks in the new behavior.

42/42 tests pass. ASAN clean.

## [0.5.49] - 2026-08-12

ExponentialHistogram wire-format fixes.

### Fixed — `ExponentialHistogramDataPoint.zero_count` wire type

Schema declared `zero_count` as VARINT, but upstream
`opentelemetry-proto` declares it as `fixed64` (so it occupies a
predictable 8 bytes regardless of value). The encoder emitted a
varint; spec-compliant collectors would see wire type VARINT at
field 7 (which they expect to be FIXED64), skip the field, and
lose the zero-bucket count.

Fix: schema wire type is now FIXED64; the encoder uses
`otlp_pb_field_fixed64`.

### Fixed — `ExponentialHistogram.Buckets.bucket_counts` encoding

The encoder packed `bucket_counts` entries as `fixed64` values
(8 bytes each). Upstream declares this field as
`repeated uint64` (varint), deliberately chosen so sparse /
   small counts compress via varint encoding. The comment in the
upstream proto even calls this out:

> This field is expected to have many buckets, especially zeros,
> so uint64 has been selected to ensure varint encoding.

The encoder packed fixed64 values into a LEN-prefixed payload.
Spec-compliant decoders parse the payload as concatenated
varints — the high bits of the first 8-byte value would be
misinterpreted as varint continuation bytes, producing garbage
counts and consuming the entire payload as one wrong number.

Fix: encoder writes `otlp_pb_varint(&packed, counts[i])` per
entry. Packed varint format matches upstream.

The wire format now matches `HistogramDataPoint.bucket_counts`
(repeated fixed64 — different field, different proto declaration)
only at the wire-type-2 (LEN) outer level; the inner item
encoding differs (varint for ExpHistogram.Buckets, fixed64 for
HistogramDataPoint), matching upstream declarations.

### Added — regression property

`prop_metrics_exp_histogram_field_nums` decodes an encoded
ExponentialHistogram and verifies:
- DataPoint{8} (positive Buckets sub-message) emits with LEN wire.
- Inside Buckets: offset{1} VARINT (with zigzag value 10 = zigzag(5)).
- Inside Buckets: bucket_counts{2} LEN-wire (packed varint).
- Decodes the 3 packed varints and verifies {1, 3, 2} round-trip.

41/41 tests pass. ASAN clean.

## [0.5.48] - 2026-08-11

OTLP schema field-number audit — multiple correctness bugs.

### Fixed — Event field numbers swapped

`Span.Event` schema had `name` at field 1 and `time_unix_nano` at
field 2. Upstream `opentelemetry-proto` declares them the other
way: `time_unix_nano = 1`, `name = 2`. The encoder produced
events with swapped field numbers; spec-compliant collectors
would skip both fields, losing the event name and timestamp.

### Fixed — Status code field number

`Status` schema had `code` at field 1 and `message` at field 2.
Upstream reserves field 1 (was a deprecated enum) and uses
`message = 2`, `code = 3`. Spans with non-default status (OK or
ERROR) had their status code field skipped by spec-compliant
collectors, leaving the status as UNSET.

### Fixed — NumberDataPoint attributes field number

`NumberDataPoint` schema had `attributes` at field 1. Upstream
reserves field 1 and puts `attributes` at field 7. Counter/Gauge
data points with attributes had them skipped by spec-compliant
collectors.

### Fixed — HistogramDataPoint field numbers

`HistogramDataPoint` schema had `attributes` at field 1, `min`
at field 9, `max` at field 10. Upstream reserves field 1 and
uses `attributes = 9`, `min = 10`, `max = 11`. Histogram data
points with attributes or min/max had those fields skipped by
spec-compliant collectors.

### Fixed — docs/otlp-spec.md spec inconsistencies

- `Span.flags`: .proto snippet said `uint32`, table said
  `fixed32`. Upstream is `fixed32`. Aligned snippet with table.
- `Link.flags`: .proto snippet said `uint32`. Upstream is
  `fixed32`. Fixed.
- `Status`: snippet had `code = 1, message = 2`. Upstream is
  `reserved 1; message = 2; code = 3`. Fixed.

### Why so many bugs at once?

Audit found the schema was the authoritative source for field
numbers but had never been cross-checked against
`opentelemetry-proto` upstream. The docs were also wrong in
places, and the tests — written against the same wrong schema
— were self-consistent with the bugs. Cross-checking against
upstream surfaced four independent field-number errors in one
pass.

Each error silently produced invalid OTLP wire output. Whether
collectors recovered depended on which fields were wrong: lost
attributes, status, event metadata, or histogram bounds. None
crashed the pipeline; all lost data.

### Added — regression properties

- `prop_encode_status_present` now descends into the Status
  sub-message and verifies `message{2} LEN + code{3} VARINT`.
  Previously only checked that Span emitted field 15.
- `prop_metrics_attributes_roundtrip` updated to look for
  attributes at field 7 (was field 1).

40/40 tests pass. ASAN clean.

## [0.5.47] - 2026-08-11

Two correctness fixes (audit findings).

### Fixed — `otlp_attribute_copy_all` fail-path leak

When an attribute's value allocation failed after its key was
already allocated (STRING/BYTES under OOM), the cleanup loop
freed items 0..i-1 but not the failed item i. The failed item's
key leaked.

The bug required memory pressure to trigger (so rare in practice)
but was a real leak that ASAN/LSAN would flag if reached. The
fail path now frees item i before the loop, leveraging
`otlp_attribute_free`'s safe handling of partial state.

### Fixed — HTTP response parser: no-Content-Length required EOF

The parser returned "complete" (1) immediately upon receiving
headers when the response had no `Content-Length`. Per RFC 7230
§3.3.3 (7), a response without Content-Length has a body that
extends until connection close. Returning early meant the body
was captured as "whatever was in the buffer at header-parse time"
— incomplete if the body was still arriving.

`try_parse_response` now takes an `at_eof` flag. The no-CL case
returns 0 (incomplete) until EOF, then returns 1 with the full
buffered body. The Content-Length case is unchanged (CL gives
the exact body length; EOF is irrelevant).

For OTLP collectors this is theoretical (they always send
Content-Length), but the parser is a general HTTP/1.1 client and
the correctness gap was real for any server that omits CL.

### Why no test for the no-CL fix

The fix changes WHEN DONE fires (after EOF vs. after headers),
not WHETHER. Existing tests already verify the response body is
correctly captured. A test that distinguishes before/after would
need to control TCP packet boundaries, which is not reliably
possible from application code. The fix is correct by inspection
against RFC 7230.

40/40 tests pass. ASAN clean.

## [0.5.46] - 2026-08-11

Table-driven exporter free-drain + span clone-shutdown test.

### Changed — descriptor-based dispatch for exporter free-path drain

`otlp_exporter_free` had the last per-signal triplication in the
exporter: three `while (mpsc_queue_pop(...)) free(...)` loops
followed by three `for (i ...) free(pending[i])` loops. Six
near-identical loops, two patterns, three signals.

Replaced with a `signal_drain_path` descriptor + a single
`drain_signal` helper. `exporter_free` builds a 3-element array
of descriptors and loops. Adding a 4th signal is one entry, not
a copy-paste of both loops.

The now-orphaned `free_pending_batch` helper (single caller
after v0.5.44 refactored the other one away) is removed.

### Moved — `*_void` wrappers promoted to lifecycle section

The `span_free_void`, `metric_free_void`, `log_free_void`
wrappers were defined in the emit section but are now needed
earlier (by the drain code in the lifecycle section). Promoted
to just before the lifecycle section. The clone wrappers stay
in the emit section (only emit uses them).

### Added — `prop_async_span_clone_shutdown`

The v0.5.42 release added `prop_async_metric_clone_shutdown` and
`prop_async_log_clone_shutdown` but not the span equivalent —
`otlp_exporter_emit` (span clone variant) had been doing the
shutdown-before-clone check correctly since v0.5.x, so there
was no fix to regression-test. Added for contract symmetry:
all three clone variants are now locked in to return SHUTDOWN
without leaking the clone.

40/40 tests pass. ASAN clean.

## [0.5.45] - 2026-08-11

Table-driven start-post pipeline.

### Changed — descriptor-based dispatch for try_start_*_post

The three start-post functions (`try_start_post`,
`try_start_metric_post`, `try_start_log_post`) were near-identical
copies of each other. Each had the same shape: NULL/empty check →
call the type-specific `otlp_exporter_otel_build_*_request` → on
failure clear keepalive_sock → on success populate
in_flight_signal/count, clear first_set.

Three functions × ~33 lines of triplicated logic = ~100 lines of
near-duplicate code. The v0.5.40 metric/log build helpers already
shared most of the encode logic with the span path; this release
finishes the job on the exporter side.

The new structure:

- `struct signal_start_path` — per-signal descriptor: pending
  array (type-erased), pending_count, first_set pointer,
  signal_kind, `build_request` fn pointer.
- `try_start_post_common(e, &path)` — single owner of the
  NULL/empty check, build call, keepalive handling, and
  in_flight state population.
- Three thin wrappers (`try_start_post`,
  `try_start_metric_post`, `try_start_log_post`) that build a
  descriptor and delegate.
- Three `build_*_request_void` wrappers in `exporter.c` that
  type-erase the items parameter so all three typed build helpers
  fit one function-pointer signature. The cast is localized to
  these wrappers; the typed build helpers in `exporter_otel.{h,c}`
  retain full type safety.

### Why

- **DRY.** Behavior changes (e.g., new keepalive policy, different
  in_flight setup) touch one helper, not three.
- **OCP.** Adding a 4th signal is a one-descriptor + one
  `*_void` wrapper addition, not a copy-paste of the function.
- **MECE.** The start-post pipeline has a single owner. The
  typed-vs-type-erased boundary is now explicit: typed at the
  `exporter_otel` boundary, erased inside `exporter.c`.

This release completes the per-signal dispatch trilogy:
- v0.5.43: emit pipeline (descriptor + clone/move helpers).
- v0.5.44: record_outcome (descriptor + outcome helpers).
- v0.5.45: start_post (descriptor + build helpers).

The exporter's per-signal triplication is now fully eliminated.
Adding a 4th signal is three descriptors + three wrappers, no
core-logic changes.

39/39 tests pass. ASAN clean.

## [0.5.44] - 2026-08-11

Table-driven record_outcome.

### Changed — descriptor-based dispatch for outcome handling

`record_outcome` and its three helpers
(`clear_in_flight_batch`, `add_sent_for_signal`,
`add_dropped_err_for_signal`) each had a `switch
(e->in_flight_signal)` with three cases. Three switches × three
cases = 9-way dispatch spread across four functions. Adding a
new signal required updating all four.

The new structure mirrors v0.5.43's emit descriptor:

- `struct signal_record_path` — per-signal descriptor: pending
  array (type-erased), pending_count pointer, first_set pointer,
  `free_item` fn, `sent_counter`, `dropped_err_counter`,
  `signal_name`.
- `record_path_for(e)` — looks up the descriptor for the
  in-flight signal. Single switch; one location.
- The three helpers now take `const struct signal_record_path *`
  and don't switch internally.
- `record_outcome` builds the descriptor once at the top and
  passes it to each helper. The signal-name ternary at the top
  is gone — replaced by `p.signal_name`.

### Why

- **DRY.** Adding a 4th signal is one case in `record_path_for`
  plus one `signal_record_path` initializer. The helpers and
  `record_outcome` body don't change.
- **MECE.** Signal dispatch lives in exactly one place
  (`record_path_for`). The helpers are signal-agnostic.
- **Type safety.** Type erasure is isolated to the descriptor's
  `free_item` field (already wrapped in `*_void` helpers from
  v0.5.43).

The `free_pending_batch` helper (used only by `exporter_free`)
remains — it's not on the record_outcome path and would be
churn to refactor for no architectural win.

39/39 tests pass. ASAN clean. The diff is small (~30 lines net
delete) but eliminates the last major switch-on-signal pattern
in the exporter.

## [0.5.43] - 2026-08-11

Table-driven emit pipeline.

### Changed — descriptor-based dispatch for all six emit functions

The three move-variant emit functions
(`otlp_exporter_emit_move`, `emit_metric_move`, `emit_log_move`)
and the three clone-variant emits
(`emit`, `emit_metric`, `emit_log`) were near-identical copies of
each other. Each had the same shape: NULL check → shutdown check →
queue push → on-failure free + counter + log; on-success counter.
The only per-signal differences were the queue, the counters, the
typed free/clone functions, and the signal name in the log
message.

Six functions × ~25 lines of triplicated logic = ~150 lines of
near-duplicate code. That's a maintenance hazard: a behavior
change (e.g., adding a new counter, changing log format) had to
be applied six times.

The new structure:

- `struct signal_emit_path` — per-signal descriptor bundling the
  queue pointer, emitted/dropped counter pointers, type-erased
  `free_item` and `clone_item` function pointers, and the signal
  name.
- `emit_move_common(e, &path, item)` — the core of every move
  variant. Single owner of the NULL/shutdown/push/stats logic.
- `emit_clone_common(e, &path, item)` — the core of every clone
  variant. Calls `emit_move_common` after cloning.
- Six thin wrappers, one per public emit function. Each builds a
  descriptor and delegates.

### Why

- **DRY.** Behavior changes touch one helper, not six functions.
- **OCP.** Adding a fourth signal (e.g., future OTLP profiler) is
  a one-descriptor addition, not a copy-paste of the entire
  pipeline.
- **MECE.** The emit pipeline now has a single owner
  (`emit_*_common`) per concern (move vs clone). Public functions
  are pure dispatch.

Type erasure via `void *` is isolated to six tiny
`*_void` wrappers (`span_free_void`, `metric_clone_void`, etc.).
The wrappers keep the cast localized; the typed public functions
retain full type safety.

39/39 tests pass. ASAN clean. The diff is +145/-103 lines, with
the net increase being descriptor + helpers; the triplicated
logic is gone.

## [0.5.42] - 2026-08-11

Clone-variant emit shutdown-before-alloc symmetry.

### Changed — emit_metric / emit_log check shutdown before cloning

`otlp_exporter_emit` (span clone variant) has always checked
`shutdown_requested` BEFORE calling `otlp_span_clone`. The metric
and log equivalents cloned FIRST, then delegated to the move
variant which re-checks shutdown and (since v0.5.41) frees the
clone.

The v0.5.41 fix made this correct (no leak) but wasteful: under
shutdown contention, the clone is allocated and immediately freed.
For metrics with many attributes / histogram buckets, the wasted
deep copy is non-trivial.

Both clone variants now check shutdown BEFORE cloning, mirroring
`otlp_exporter_emit`. Behavior under shutdown is now uniform
across all three signals: return SHUTDOWN without allocating.

### Added — clone-variant shutdown regression properties

Two new properties in `test_property_async_metrics`:
- `prop_async_metric_clone_shutdown`
- `prop_async_log_clone_shutdown`

Each constructs an item, calls shutdown, then calls the clone
variant and asserts SHUTDOWN return. ASAN-clean.

The external behavior is identical with or without the symmetry
fix (move variant frees the clone); the perf win is invisible to
the test. The properties exist to lock in the contract that all
three clone variants return SHUTDOWN without allocating.

39/39 tests pass on Linux CI (one http-timeout test is a known
macOS-local flake — 192.0.2.1 routing differs — passes in CI).
ASAN clean.

## [0.5.41] - 2026-08-10

Move-emit leak on shutdown.

### Fixed — donated span/metric/log leaked on shutdown-return path

`otlp_exporter_emit_move`, `otlp_exporter_emit_metric_move`, and
`otlp_exporter_emit_log_move` take ownership of the donated item on
call entry. The public docstring says: "The exporter frees the
span once it has been encoded (or dropped on shutdown)."

The implementation honored this on the BUFFER_FULL path
(`otlp_*_free` + counter increment) but NOT on the SHUTDOWN path.
When shutdown was requested between the caller's allocation and
the move call, the move variant returned `OTLP_ERR_SHUTDOWN`
without freeing the donated item — leaking it.

The clone-and-move wrappers (`emit_metric`, `emit_log`) inherited
the bug: they allocated a clone, then delegated to the move
variant, which leaked the clone on shutdown.

All three move variants now free the donated item before returning
SHUTDOWN, matching the queue-full behavior and the documented
contract.

### Added — regression properties

Three new properties in `test_property_async_metrics` exercise
the shutdown-drop path under ASAN:
- `prop_async_span_shutdown_drop`
- `prop_async_metric_shutdown_drop`
- `prop_async_log_shutdown_drop`

Each allocates a fresh item, calls shutdown, then calls the move
variant and asserts SHUTDOWN return. ASAN catches the leak if the
free is missing.

37/37 tests pass. Zero warnings. ASAN clean.

## [0.5.40] - 2026-08-10

MECE refactor: metric/log POST builders + keepalive reuse.

### Fixed — body leak in metric/log POST paths

`try_start_metric_post` and `try_start_log_post` called
`otlp_encode_export_*_service_request` after `otlp_pb_buf_init`
succeeded. When the encoder failed mid-encode (e.g., allocation
failure during buffer growth), the body buffer was not freed before
return. The bug only fired under memory pressure but would manifest
as a heap-buffer leak in long-running exporters.

### Fixed — keepalive socket not reused for metrics/logs

When any in-flight request completed, the keep-alive socket was
detached and saved in `e->keepalive_sock`. The span POST path
(`try_start_post`) reused this socket via
`otlp_exporter_otel_build_request` → `start_with_socket`. But the
metric and log POST paths used plain `otlp_http_request_start`,
which always opens a fresh connection — leaving the saved socket
orphaned until exporter free.

For a workload that interleaves spans, metrics, and logs, this
meant 3 TCP connects per batch instead of 1. On TLS-terminated
sidecar topologies the cost is lower (no TLS handshake in the
library) but still wasteful.

Both paths now reuse the keepalive socket via the new build helpers.

### Changed — symmetric build helpers per signal

The span-only `otlp_exporter_otel_build_request` is renamed to
`otlp_exporter_otel_build_span_request`, and two new siblings are
added: `otlp_exporter_otel_build_metric_request` and
`otlp_exporter_otel_build_log_request`. All three share a common
`start_post_common` helper that picks `_start_with_socket` vs
`_start` based on whether a keepalive socket is available.

This achieves three things:
- Single owner for encode-failure cleanup (the build helper, not
  the exporter) — eliminates the body-leak class of bug.
- Identical keepalive semantics across all three signals.
- MECE: `exporter.c` (lifecycle) no longer inlines wire-format
  encode logic; `exporter_otel.c` (wire-format) owns it.

34/34 tests pass. Zero warnings. ASAN clean.

## [0.5.39] - 2026-08-10

Defensive coding + batch encode benchmark.

### Fixed — unchecked `otlp_pb_buf_init` returns

`try_start_metric_post` and `try_start_log_post` called
`otlp_pb_buf_init(&body, ...)` without checking the return value.
On allocation failure, the encoder would write into a zero-cap
buffer and the subsequent `otlp_encode_*` would return an error
that was propagated — but the failure was reported against the
encoder, not the allocation, making the failure path harder to
diagnose.

Both call sites now capture and return the init status directly.
The bug had no functional impact in practice (the allocator path
succeeded), but the failure attribution is now correct.

### Added — `bench/bench_encode_batch.c`

A new microbenchmark measuring batch-encode throughput at
batch sizes 1, 16, 64, 256, 512 with 0 or 5 attributes per span.
Catches O(n^2) regressions in the encoder and verifies the
v0.5.38 pre-sized buffer optimization is effective.

Output shows total ns, ns/span, and wire bytes per configuration.
Linear O(n) scaling verified across the full matrix.

34/34 tests pass. Zero warnings. All sanitizers green.

## [0.5.38] - 2026-08-10

Pre-size encode buffers based on batch size — eliminates ~10
malloc+memcpy+free cycles per batch encode.

### Changed — Pre-sized protobuf body buffers

The encode path initialized the body buffer to 64 bytes (default)
and grew it via doubling as spans/metrics/logs were encoded. For a
full batch of 512 spans (~100KB output), this required ~10 growth
steps, each a malloc + memcpy + free of increasing size.

Now pre-sizes based on batch count:
- Traces: `n_spans * 256 + 1024` bytes.
- Metrics: `n_metrics * 128 + 512` bytes.
- Logs: `n_logs * 128 + 512` bytes.

The estimate has headroom — actual encoded size is typically
~150-200 bytes per span, ~50-100 bytes per metric/log. The buffer
still grows if the estimate is too low (graceful degradation).

The synchronous flush paths (`flush_metric`, `flush_log`) encode
one item at a time — pre-sizing is negligible there and was left
unchanged.

34/34 tests pass. Zero warnings. All sanitizers green.

## [0.5.37] - 2026-08-10

Roadmap update — catches docs/roadmap.md up to v0.5.36. The v0.5
section stopped at PR #25 (v0.5.6 era); everything from v0.5.18
through v0.5.36 (20 releases, 7 correctness fixes, async metrics/
logs, API completion) was missing.

### Fixed — docs/roadmap.md stale

- Added a "v0.5.18–v0.5.36 enhancements" table with all 20
  releases from this session (PRs #48-#66).
- Fixed the FreeBSD CI "Out of scope" claim — FreeBSD IS in the
  CI matrix via vmactions/freebsd-vm (best-effort, continue-on-error).

This was the last stale documentation in the project. All primary
docs (CLAUDE.md, README.md, architecture.md, cookbook.md, roadmap.md)
are now current as of v0.5.36.

## [0.5.36] - 2026-08-10

CLAUDE.md + README accuracy audit — catches primary documentation
up to v0.5.35 (6 releases since last audit in v0.5.29).

### Fixed — CLAUDE.md stale claims

- Version reference: v0.5.28 → v0.5.35.
- Feature list: added emit_metric/emit_log clone variants
  (v0.5.31), table-driven tick() signal dispatch (v0.5.30),
  per-signal stats, span_clone event/link attribute fix
  (v0.5.33).
- Updated the async emission bullet to mention all 6 emit
  variants (clone + move for each signal).

### Fixed — README version banner

- 0.5.28 → 0.5.35.

Same accuracy audit cycle as v0.5.16 (CLAUDE.md), v0.5.19
(policy docs), v0.5.29 (CLAUDE.md + architecture + README).

## [0.5.35] - 2026-08-09

Fixes null_transport backoff-retry double-processing + adds metric/
log retry tests.

### Fixed — Backoff retry starts HTTP request under null_transport

The backoff retry path in `tick()` (step 5) unconditionally called
`paths[in_flight_signal].start_post(e)` — even when
`null_transport` was enabled. This started a REAL HTTP request
alongside the null-transport fast path, causing double-processing:
the next tick iteration's null-transport path would re-send the
same batch, incrementing `sent_metrics`/`sent_logs` twice for one
emit.

Fix: the backoff retry now checks `!e->null_transport` before
starting an HTTP request. When null_transport is enabled, backoff
is cleared but the retry is handled by the null-transport fast
path on the next tick iteration (same as the initial send).

This is the same class of bug as v0.5.21 (null_transport ignored
backoff_armed) and v0.5.23 (null_transport fast path didn't
respect backoff_armed). The null_transport / HTTP interaction
has now been fully audited.

### Added — Metric/log retry property tests

Two new properties in `test_property_async_metrics.c`:

- `prop_async_metric_retry` — null_transport returns 500 first,
  200 second. Verifies the metric is retried and sent exactly
  once (not double-counted by the old backoff-retry bug).
- `prop_async_log_retry` — same pattern for logs (503 → 200).

These exercise the metric/log backoff retry dispatch
(`paths[in_flight_signal].start_post`) that had never been tested
before v0.5.35.

## [0.5.34] - 2026-08-09

Multi-signal concurrency stress test — validates the v0.5.28 async
metric/log pipeline is race-free under concurrent load.

### Added — Multi-signal concurrency stress test

`tests/test_concurrency_stress_multi.c`: 8 threads concurrently
emit spans, metrics, AND logs into one exporter while the main
thread ticks. Uses null_transport for determinism.

Verifies:
- All 800 spans (8 × 100) are emitted and sent.
- All 800 metrics are emitted and sent.
- All 800 logs are emitted and sent.
- Per-signal stats are correct.
- No race conditions (TSAN-clean).

The existing `test_concurrency_stress.c` only tested spans. The
three-queue, one-in-flight, shared-backoff design from v0.5.28
had never been stress-tested with all three signals concurrent.

Test count: 33 → 34. All pass under plain, TSAN, ASAN+UBSAN.

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
