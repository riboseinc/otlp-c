# TODO 130 — Coverage actually measured; exporter_otel gap closed

**Status:** Complete (v0.5.90)
**Priority:** P1 (unmeasured code is where bugs hide)

## What shipped

**The bug:** `OTLP_C_ENABLE_COVERAGE` never instrumented
anything. The flags were added via directory-scope
`add_compile_options` at line ~251, but the `otlp_c` target is
defined at line ~95 — CMake applies directory-scope compile
options only to targets created later, so every coverage build
produced empty profiles (verified: no `__llvm_profile` symbols,
no `.profraw`). The option had been advertised since the
coverage phase.

**Fix:** target-scoped `target_compile_options(otlp_c PUBLIC
-fprofile-instr-generate -fcoverage-mapping -O0 -g)` +
`target_link_options(... -fprofile-instr-generate)` — PUBLIC
propagates to every test binary linking the library.

**First real measurement** (all 38 test runs merged):

| file | regions covered |
|---|---|
| exporter_otel.c | **30%** ← the gap |
| platform_unix.c | 76% (error branches) |
| tracer.c | 81% |
| sampler.c | 86% |
| mpsc_queue.c | 95% |
| everything else | 95%+ |

The exporter_otel miss: every prior test exercised spans over
HTTP or any signal via null_transport — which skips
request-building entirely. The metric/log POST-build paths had
zero coverage.

**Tests added:** exporter-echo cases 2 and 3 — real-HTTP async
metric (`emit_metric_move` + flush) and log (two records, batch)
exports against the echo server, asserting emitted == sent per
signal. exporter_otel.c: 30% → **75% regions / 100% functions**
(remaining misses are OOM/failure branches).

## Reproduce the measurement

```
cmake -B build-cov -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_COVERAGE=ON
cmake --build build-cov
cd build-cov
LLVM_PROFILE_FILE="$PWD/cov-%p.profraw" ctest -E http-timeout
xcrun llvm-profdata merge -o cov.profdata cov-*.profraw
BINS=$(find . -type f -perm +111 -name "otlp_*" | grep -v CMakeFiles | tr '\n' ' ')
xcrun llvm-cov report $BINS -instr-profile=cov.profdata
```
