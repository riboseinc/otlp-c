# TODO 162 — OTel env vars, working presets, legacy removal

**Status:** Complete (v0.7.0)
**Priority:** P1 (feature) + P2 (build ease, legacy)

## Features: OTel standard environment variables

`otlp_exporter_opts_apply_env(opts, buf, cap)` — additive public
API. Supported: OTEL_EXPORTER_OTLP_ENDPOINT (base form →
"/v1/traces" appended), _TRACES_ENDPOINT (full URL, wins),
_TIMEOUT (ms → connect+read), _PROTOCOL ("http/protobuf" only,
else INVALID_ARGUMENT), OTEL_SERVICE_NAME. Pure parsers in
src/env_config.{h,c} take value strings (unit-tested without env
mutation); one getenv driver. Composed endpoints land in the
caller's buffer (opts.endpoint points into it — documented
lifetime requirement). Unset variables pass through, so it
composes with hand-filled opts. Not supported (documented):
HEADERS, per-signal metric/log variables.

## Build ease: presets that actually work

The existing CMakePresets.json was INVALID — top-level
generator/configureOnPress/cacheVariables fields (not in the
schema); `cmake --preset` failed outright. Never-executed
legacy, like the vcpkg port before it. Rewritten (schema v3,
CMake 3.21+): default/release/asan/ubsan/tsan configure+build+test
presets with separated binary dirs, validated end-to-end locally.
The ASAN test preset does NOT force detect_leaks=1 — that option
aborts on macOS (Linux CI remains the leak gate; the macOS-ASAN
memory note applies).

## Legacy deleted (explicit user instruction: "delete all legacy")

- src/arena.{c,h}: never in any build target, included by
  nothing (slab.c has its own inline arena). git-rm'd;
  recoverable from history.
- Swept the tree for other orphans: every remaining .c is in a
  build target; every remaining .h is included (w3c.c was a
  false positive of the name pattern — it contains a digit).

## Performance

Baseline re-verified after the v0.6.11-13 refactor arc (Debug):
emit ~163 ns/span (0 attrs), build+move ~249-947 ns, encode
~300 ns + ~145 ns/attr, linear — unchanged from the historical
numbers. No optimization taken without a measured need.

## Verification

52/52 via every preset (default/release/asan/ubsan/tsan);
Release zero warnings; Doxygen zero warnings.
