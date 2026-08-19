# TODO 124 — Send-stall timeout + sampler endianness

**Status:** Complete (v0.5.84)
**Priority:** P1 (robustness: send-side slowloris; correctness: platform-dependent sampling identity)

## What shipped

**1. The SENDING phase had no deadline (send-side slowloris).**
`connect_timeout_ms` covered CONNECTING; `read_timeout_ms` covered
READING; but a server that accepts the connection and never reads
the POST left SENDING blocked forever once the kernel send buffer
filled — the caller's tick loop would spin indefinitely. The
inactivity deadline now covers SENDING too: `last_recv_ms` became
`last_io_ms` (reset on connect completion, every partial write,
and every recv), and `step_sending` fails with `OTLP_ERR_TIMEOUT`
when no send progress occurs within `read_timeout_ms`. A
slow-but-steady stream (or peer) still never trips it — the timer
measures inactivity, not duration. `http_client.h` now documents
the semantics precisely (inactivity deadline across SENDING and
READING; no total cap; 0 = infinite).

Pinned by `test_send_stall_times_out` — a sink server (accepts,
never reads) plus a 4 MB POST must fail with TIMEOUT. Before the
fix this test hangs.

**2. The ratio sampler read the trace-ID prefix with native
endianness.** `memcpy(&prefix, trace_id, 8)` is byte-reversed on
little-endian, so while the sampling *rate* was correct on every
platform (uniform IDs), *which* IDs sample was platform-dependent
— contradicting the comment's cross-SDK claim. Now an explicit
big-endian load, matching otel-go's
`binary.BigEndian.Uint64(traceID[0:8])`. Pinned by
`prop_ratio_endian_known_answer`: at ratio 0.5, an ID starting
0x80.. is not sampled, 0x7F.. is — on every platform. (The
distribution properties pass unchanged, confirming rate
invariance.)

**Process finding (recorded in memory):** the send-stall test's
first drafts hit the NDEBUG assert-side-effect trap FOUR times in
one function — `bind`/`listen`/`getsockname` inside asserts (the
socket was never bound under Release → connect refused), and
`otlp_http_parse_url(&url)` inside an assert (the URL stayed
garbage). Plus a pacing corollary: iteration-bounded drive loops
spin ~100× faster in Release, so waiting on a 120ms timeout needs
a wall-clock bound. All fixed with explicit rc checks; the test
header and the memory note document the pattern.

## Sites changed

- `src/http_client.c` — `last_io_ms` semantics, send-phase idle
  timeout, connect-completion progress reset.
- `src/http_client.h` — timeout docs.
- `src/sampler.c` — big-endian prefix load.
- `tests/test_http_parser.c` — send-stall test (sink server,
  rc-checked setup, wall-clock-bounded drive loop).
- `tests/property/test_property_sampler.c` — endian known-answer.

## Verification

```
cmake --build build && cmake --build build-rel     # zero warnings
ctest --test-dir build -E http-timeout             # 38/38 (Debug)
ctest --test-dir build-rel -E http-timeout         # 38/38 (Release)
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -E "http-timeout|url-parse"
./build/tests/otlp_test_http_parser                # 10/10, ×8 stable in Release
```
