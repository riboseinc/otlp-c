# TODO 81 — Move-emit leak on shutdown

**Status:** Complete (v0.5.41)
**Priority:** P1 (correctness: leak in user-facing API)

## What shipped

Fixed a memory leak in all three move-emit variants. The move
contract (per public docstring) says the library frees the donated
item "once it has been encoded (or dropped on shutdown)". The
implementation honored this on the BUFFER_FULL path but NOT on the
SHUTDOWN path — the donated item was leaked when shutdown was
detected between caller allocation and the move call.

## Sites changed

- `src/exporter.c::otlp_exporter_emit_move` — `otlp_span_free(span)`
  before returning `OTLP_ERR_SHUTDOWN`.
- `src/exporter.c::otlp_exporter_emit_metric_move` — same.
- `src/exporter.c::otlp_exporter_emit_log_move` — same.
- `tests/property/test_property_async_metrics.c` — three new
  properties (`prop_async_*_shutdown_drop`) that exercise the path.

## Why this matters

The clone-and-move wrappers (`emit_metric`, `emit_log`) inherited
the bug. They allocate a clone, then delegate to the move variant.
If the move variant leaks on shutdown, the clone is leaked — even
though the caller used the "safe" non-move API.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure    # 37/37 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan                   # ASAN clean
```

The new properties explicitly call `shutdown()` before the move
variant. Without the fix, ASAN reports a leak at exporter free;
with the fix, the properties pass clean.
