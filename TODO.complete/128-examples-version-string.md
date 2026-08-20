# TODO 128 — Examples audit; runtime version string drift fix

**Status:** Complete (v0.5.88)
**Priority:** P1 (runtime API returned a wrong version for 19 releases)

## What shipped

**The bug the examples audit surfaced:** running the refreshed
`minimal.c` printed `otlp-c 0.5.68` — on a 0.5.87 tree.
`OTLP_C_VERSION_STRING` was a hand-maintained literal, last
touched at v0.5.68; every subsequent release bumped the numeric
macros (and CMakeLists + vcpkg.json) but not the string, so the
public `otlp_version()` API has been lying for nineteen releases.
Nothing caught it: the release checklist checks the three FILES
agree, and no test compared the string to the macros.

**Fix (structural, not a one-off edit):** the string is now
DERIVED from the macros via token-pasting
(`OTLP_C_STR(OTLP_C_VERSION_MAJOR) "." …`), so it cannot drift
again. A new smoke-test assertion parses `otlp_version()` against
the numeric macros — belt and braces.

**Examples audit (the rest — all clean, now better):**
- `multithread.c` — verified against the v0.5.82 concurrency
  contract: workers join BEFORE flush+free, exactly right. The
  header now SAYS why (shutdown is a cooperative signal, not a
  barrier; join-first is what makes free() safe) and notes
  `emit_move` as the clone-free hot-path alternative.
- `minimal.c` — its header claims "full API surface" but predated
  v0.5.74; now demonstrates the composite ArrayValue attribute
  (`otlp_span_set_attribute_array` from `otlp_value_t`) and the
  map semantics (re-setting `http.status` replaces the value).
- Both examples verified running (null_transport; multithread
  asserts emitted == sent == expected).

## Sites changed

- `include/otlp-c/version.h` — derived string.
- `tests/test_smoke.c` — version consistency assertion.
- `examples/minimal.c`, `examples/multithread.c` — refresh above.

## Verification

```
cmake --build build && cmake --build build-rel     # zero warnings
ctest --test-dir build -E http-timeout             # 38/38 (Debug)
ctest --test-dir build-rel -E http-timeout         # 38/38 (Release)
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -E "http-timeout|url-parse"  # 37/37
./build-ex/examples/otlp_example_minimal           # "otlp-c 0.5.88"
./build-ex/examples/otlp_example_multithread       # 1000/1000 PASS
```
