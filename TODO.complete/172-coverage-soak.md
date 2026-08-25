# TODO 172 — Coverage CI-enforced; 100k property soak

**Status:** Complete (v1.0.4)
**Priority:** P2 (automating the bar; soak validation)

## Coverage re-measured (first time since v0.6.2)

Every library file clears the 82% region floor; the 1.x-era
modules comfortably: http_response_parser 98.2%, env_config
89.1%, retry_policy 84.2%, exporter 89.1%, exporter_otel 86.8%,
exemplar paths within metric.c 83.4%. No fill work needed —
the discipline held across the whole 0.7-1.0 arc.

## CI gate

New ubuntu job (coverage): clang coverage preset, full suite,
llvm-profdata/llvm-cov report, and a parser that fails any
src/ or include/ file below 82% regions. The bar can no longer
drift unnoticed.

## Soak

- 21/25 property BINARIES at OTLP_C_PROPERTY_ITERS=100000:
  zero assertion failures.
- The other four (keepalive, flush-timeout, http-timeout,
  async-metrics) sleep through real waits per iteration — 10k
  runs take tens of minutes at 0.3% CPU; their default 1000 is
  the practical soak. Tiered protocol documented in CLAUDE.md.
- ctest discovery: per-test TIMEOUT 60 pre-dates the soak flow;
  `ctest --timeout` does NOT override a set TIMEOUT property on
  this cmake — soaks run binaries directly.

## README banner

"0.5.35 … the API surface is unstable until 1.0.0" — stale on
the front page since v1.0.0, contradicting the freeze. Fixed:
1.0.x, API frozen, feature list current (exemplars, schema_url,
headers, env vars).
