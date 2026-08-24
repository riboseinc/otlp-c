# TODO 171 — Conformance gates CI-enforced; 1.x surface live

**Status:** Complete (v1.0.3)
**Priority:** P1 (making the v1.0.1/1.0.2 disciplines automatic)

## CI job: conformance-gates (ubuntu)

- pip install opentelemetry-proto==1.44.0 (pinned to the version
  the goldens were generated with).
- tests/golden/audit_tables.py must exit 0 (all 32 tables match
  the installed descriptors).
- generate.py regenerates; git diff --exit-code on tests/golden/
  — stale vectors, fixture edits without regeneration, and
  upstream proto changes all fail the build.
- Regeneration verified idempotent locally (clean tree after
  re-run).

## Integration: the full 1.x surface, live

New scenario in the Jaeger integration test — one exporter with:
schema_url, an extra HTTP header, env-var opts via apply_env
(OTEL_EXPORTER_OTLP_TIMEOUT=8000, asserted), a per-signal
metrics_endpoint, and a counter carrying an exemplar (double +
trace/span + timestamp). Gate: 2xx from the real otelcol — its
protobuf parser would reject the wrong exemplar field numbers
that shipped in v0.8.0.

(Local Docker daemon was down at release time; the CI job is the
authoritative runner and gated this PR.)

## Verification

52/52 via every preset locally; yaml validated; gates green in
CI on the release PR.
