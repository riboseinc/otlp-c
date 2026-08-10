# TODO 80 — Metric/log POST builders + keepalive reuse

**Status:** Complete (v0.5.40)
**Priority:** P2 (correctness + performance)

## What shipped

Three related fixes bundled because they all live in the same code
path and the right fix for any one of them is the same refactor that
fixes all three:

1. **Body leak.** `try_start_metric_post` and `try_start_log_post`
   leaked the protobuf body buffer when the encoder failed
   mid-encode (after `otlp_pb_buf_init` succeeded).
2. **No keepalive reuse for metrics/logs.** The saved keepalive
   socket from any completed request was only ever reused by span
   POSTs. Metric and log POSTs always opened a fresh connection.
3. **Asymmetry.** Spans went through `exporter_otel.c`; metrics and
   logs inlined their own encode-and-start in `exporter.c`. Three
   different code paths for the same conceptual operation.

## The refactor

`exporter_otel.c` now owns all three build paths:

- `otlp_exporter_otel_build_span_request` (renamed from
  `otlp_exporter_otel_build_request` — one caller, easy rename).
- `otlp_exporter_otel_build_metric_request` (new).
- `otlp_exporter_otel_build_log_request` (new).

All three share `start_post_common` which picks `_start_with_socket`
vs `_start` based on whether the caller supplied a keepalive socket.
All three free the body buffer on encode failure.

`exporter.c::try_start_metric_post` and `try_start_log_post` are now
thin wrappers that thread `e->keepalive_sock` through the helpers,
mirroring what `try_start_post` already did.

## Sites changed

- `src/exporter_otel.h` — add two declarations, rename existing.
- `src/exporter_otel.c` — implement three builders + shared helper.
- `src/exporter.c` — `try_start_metric_post`, `try_start_log_post`
  delegate to the new helpers; `try_start_post` uses renamed fn.

## Test gap

A test that confirms metric/log POSTs actually reuse the saved
keepalive socket would require intercepting at the platform-socket
layer to count TCP connects. The existing `test_property_keepalive`
properties validate the HTTP-client-level mechanism (detach +
donate), and `test_property_async_metrics` validates end-to-end
metric emit + send. The bug being fixed (extra connects) is not
detectable by either — it's a performance regression, not a
correctness one.

A future property test could wrap `otlp_socket_connect` with a
counter via the custom-allocator hook (once that lands — see TODO
28). For now, the refactor stands on its own: symmetric code,
correct cleanup, no behavior change for callers.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_BUILD_EXAMPLES=ON
cmake --build build                              # zero warnings
ctest --test-dir build --output-on-failure       # 34/34 pass
cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan                      # ASAN clean
```
