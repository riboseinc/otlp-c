# otlp-c — domain glossary

One line per term, a pointer to its home. The shared vocabulary
for reviews, docs, and agents — CLAUDE.md carries the engineering
rules; this file carries the language. Do not put policy here.

| Term | Meaning | Home |
|---|---|---|
| signal | One of the three OTLP streams: traces, metrics, logs (a 4th, profiles, is a future table row) | `SIGNAL_SPECS[]`, src/exporter.c |
| exporter | The caller-tick pipeline: 3 MPSC queues → batch → POST → outcome | src/exporter.c |
| tick | The caller-driven progress call; the library never spawns a thread | `otlp_exporter_tick()` |
| span | The traces datum: name, ids, timing, attributes, events, links, status | include/otlp-c/span.h |
| exemplar | A value captured at a moment, optionally tied to the trace/span that produced it | include/otlp-c/metric.h |
| resource attribute | Process-constant key/value on every batch (service.name first) | opts.resource_attributes |
| attribute surface | Any of the six attribute-bearing owners (span, event, link, metric, log, resource) — one set-attribute engine behind all | src/internal_util.c |
| value model | `otlp_value_t` — the seven AnyValue types; every attribute value is one | include/otlp-c/value.h |
| spec table | One `OTLP_*_FIELDS` entry: the wire numbers for one message | src/otlp_schema.h |
| descriptor audit | tests/golden/audit_tables.py — every spec table vs the installed opentelemetry-proto descriptors | tests/golden/ |
| golden vector | A reference-serialized payload; our encode (and, for responses, decode) must byte-match it | tests/golden/generate.py |
| wire pin | unit-wire-numbers' literal check of a spec table (documents; goldens verify) | tests/unit/test_unit_wire_numbers.c |
| event | `otlp_event_t` — the one diagnostics model; string messages are derived from it | include/otlp-c/exporter.h |
| null transport | The in-memory send path for deterministic tests | `otlp_exporter_set_null_transport()` |
| retry policy | Pure timing functions: full jitter, Retry-After floor, cap | src/retry_policy.{h,c} |
| response parser | Pure HTTP/1.1 response wire-format module (smuggling rejection lives here) | src/http_response_parser.{h,c} |
| signal table | `SIGNAL_SPECS[]` + `sig[3]`: everything per-signal, one place | src/exporter.c |
| sidecar | The otelcol process terminating TLS for us (no TLS in-library, by ADR) | docs/deployment.md |
| consumer project | A standalone CMake project under tests/consumers pinning one consumption mode | tests/consumers/ |
| overlay port | The in-repo vcpkg recipe building the local checkout | ports/otlp-c/ |
| freeze | Since 1.0.0: source API additions-only for 1.x; C ABI not guaranteed | include/otlp-c/version.h |
