# TODO 129 — Fuzz expansion; second-lap spot audits (clean)

**Status:** Complete (v0.5.89)
**Priority:** P2 (confidence: the newest untrusted-bytes surface was only hand-tested)

## What shipped

**Fuzz coverage for the response parser** — the one input surface
the fuzz harness didn't touch, and the newest (v0.5.80 chunked
decoder). Two new properties in `test_property_fuzz.c`:

- `prop_fuzz_http_response` — builds random (pure bytes) or
  mutated (byte flips / truncation / extension of a VALID chunked
  response, including the size-line and trailer sections) raw
  responses, serves them through the real state machine via the
  `ECHO_RAW_RESPONSE` echo mode, and asserts the request ALWAYS
  reaches DONE or FAILED within a wall-clock bound — never a
  hang, never a crash. 300 iterations by default (socket-bound);
  verified at 5,000 iterations locally, including under ASAN +
  LeakSanitizer.
- `prop_fuzz_context_extract` — a carrier returning arbitrary
  printable bytes for traceparent/tracestate/baggage; extract
  never crashes (5,000 iterations; complements v0.5.81's targeted
  control-byte property with arbitrary-content fuzzing).

The fuzz target links the echo helper (new dependency) and gains
a parent-directory include path.

**Second-lap spot audits (both clean, findings documented as
verified-correct):**
- `src/common.c` `otlp_strerror()` — coverage diffed against the
  status enum: every `OTLP_ERR_*` case has a message (the one
  diff artifact was a grep truncation on `UTF8`, not a gap).
- `src/platform_unix.c` socket wrappers — EINTR retry loops,
  EAGAIN/EWOULDBLOCK → `OTLP_ERR_WOULDBLOCK` mapping, EOF via
  `recv()==0` + `s->eof` flag with `n_read==0`/OK semantics the
  client's `at_eof` plumbing relies on, `MSG_NOSIGNAL` on send:
  all correct. No changes needed.

## Sites changed

- `tests/property/test_property_fuzz.c` — two properties + echo
  wiring.
- `tests/property/CMakeLists.txt` — link helper, include dir.

## Verification

```
ctest --test-dir build -E http-timeout          # 38/38 (Debug)
ctest --test-dir build-rel -E http-timeout      # 38/38 (Release)
OTLP_C_PROPERTY_ITERS=5000 ./build/tests/property/otlp_property_fuzz
ASAN_OPTIONS=detect_leaks=1 ./build-asan/tests/property/otlp_property_fuzz
```
