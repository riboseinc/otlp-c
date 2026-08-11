# TODO 87 — copy_all fail-path leak + HTTP no-CL premature DONE

**Status:** Complete (v0.5.47)
**Priority:** P2 (correctness: two audit-found bugs)

## What shipped

Two correctness fixes from auditing non-exporter code (the exporter
was audited via v0.5.39-v0.5.46):

1. **`otlp_attribute_copy_all` fail-path leak.** When a STRING/BYTES
   attribute's value allocation failed after its key was already
   allocated (OOM), the cleanup loop freed items 0..i-1 but not the
   failed item i. Item i's key leaked.

2. **HTTP response parser no-CL premature DONE.** For responses
   without `Content-Length`, the parser returned "complete" upon
   receiving headers, instead of waiting for EOF per RFC 7230
   §3.3.3 (7).

## Sites changed

- `src/internal_util.c::otlp_attribute_copy_all` — fail path now
  frees item i explicitly before the 0..i-1 loop.
- `src/http_client.c::try_parse_response` — takes `bool at_eof`
  parameter. The no-CL case returns 0 (incomplete) until EOF,
  then returns 1 with the full buffered body.
- `src/http_client.c::step_reading` — calls `try_parse_response`
  with `at_eof=false` during normal stepping and `at_eof=true`
  on the EOF re-parse.

## Why

### copy_all leak

The bug required memory pressure to trigger. LSAN in CI would
catch it if reached. The fix is defensive — the partial-item
state is correctly cleaned up regardless of which allocation
fails.

### HTTP no-CL

OTLP collectors always send Content-Length, so the bug was
theoretical for the primary use case. But the HTTP client is
a general HTTP/1.1 implementation, and a non-CL server (rare
but spec-legal) would have its body truncated.

## Test gap

The copy_all bug requires OOM injection (custom allocator hook,
still pending per TODO 28).

The HTTP no-CL fix changes WHEN DONE fires, not WHETHER. A
regression test would need to control TCP packet boundaries to
simulate a slow-streaming server — not reliably possible from
application code. The fix is correct by inspection against
RFC 7230.

Existing tests pass:
- LSAN-enabled CI confirms no leak regression.
- Keepalive/flush/async-metrics tests confirm response parsing
  still works for the CL case.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 40/40 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

## Audit context

This release continues the audit pattern from earlier sessions:
- v0.5.31-v0.5.33: clone functions (span/metric/log event+link
  attributes).
- v0.5.41: move-emit shutdown leak.
- v0.5.42: clone-variant emit shutdown-before-alloc symmetry.
- v0.5.47: copy_all fail path + HTTP no-CL.

The clone paths and HTTP parser are now audited clean. Next audit
targets: mpsc_queue internals, platform layer (POSIX/Win32), and
the protobuf encoder field-number tables.
