# TODO 59 — Test infrastructure TSAN races

**Status:** Complete (v0.5.18)
**Priority:** P0 (CI-blocking)

## What happened

The TSAN CI job added in [[56-ubsan-tsan-ci]] (v0.5.15) flagged
three tests as data races. The failures blocked PR #48 (and would
block any future PR) from getting a green TSAN signal. CI was
"green" only by virtue of `continue-on-error` semantics on certain
flaky platforms; the TSAN job itself was hard-failing.

The three failing tests:

- `http-echo` (`tests/test_http_echo.c`)
- `concurrency-stress` (`tests/test_concurrency_stress.c`)
- `property-keepalive` (`tests/property/test_property_keepalive.c`)

All three shared one root cause.

## Root cause

The in-process HTTP echo server (`tests/test_helper_echo.{h,c}`)
and the inline mini-server in `test_property_keepalive.c` both
used plain `bool` / `int` / `size_t` fields for cross-thread state:

```c
struct echo_server {
    bool running;              /* written by worker, polled by main */
    size_t requests_served;    /* written by worker, read by main */
    size_t requests_seen;
    ...
};
```

The worker thread (created via `pthread_create`) wrote these
fields; the test main thread read them via either:

1. `echo_server_join()`'s polling loop (which used `nanosleep`
   between checks), or
2. Direct read after `echo_server_join` returned.

**`nanosleep` is not a synchronization primitive.** TSAN correctly
identified that the worker's writes and main's reads had no
happens-before edge — the apparent "sleep long enough" pattern is
a timing assumption, not synchronization.

## The fix

Made every cross-thread field atomic via `../src/atomic_compat.h`
(the project's standard atomic abstraction layer). Three memory
orderings matter:

| Access | Ordering | Why |
|---|---|---|
| Worker: `running = 0` on exit | RELEASE | Pairs with main's ACQUIRE load; makes all prior writes (including the final `requests_served`) visible. |
| Main: `running` poll in `_join` | ACQUIRE | Establishes the happens-before edge with the worker's exit. |
| Main: `requests_served` read after join | RELAXED | The ACQUIRE load above already carried the needed sync; this is just a plain read of a value known to be stable. |
| Worker: `requests_served++` | RELAXED | No ordering needed intra-thread; the RELEASE store of `running=0` carries the increment visibility. |

### Files changed

- `tests/test_helper_echo.h` — struct fields → atomic types.
- `tests/test_helper_echo.c` — all accesses go through `otlp_atomic_*`.
- `tests/property/test_property_keepalive.c` — `mini_srv.requests_served`
  atomicized; ALSO reordered the increment to happen BEFORE `send()`.
  This is a logical correctness fix on top of the TSAN fix: by the
  time main's `recv()` returns the response, the counter has already
  advanced. With the previous ordering (increment after send), main
  could observe the response body but read a stale counter.
- `tests/test_concurrency_stress.c` — `srv.requests_served` reads
  converted to `otlp_atomic_load_u64`.

## Also fixed — pre-existing compiler warnings

The clean rebuild surfaced two pre-existing warnings that hadn't
been noticed because CI doesn't use `-Werror`:

1. `src/internal_util.h:14` — comment text contained `src/*.c`,
   and clang's `-Wcomment` flagged the `/*` as a potential nested
   block-comment open. Present since v0.4. Rephrased to "source .c
   files under src/".
2. `tests/test_exporter_echo.c:34` — dead `static int requests_seen`
   counter inside `count_handler`, incremented but never read.
   `-Wunused-but-set-variable`. Removed.
3. `tests/property/test_property_seed.c:25` — `prop_version_consistent`
   takes a `seed` parameter it doesn't use (the property checks a
   constant). `-Wunused-parameter`. Marked `(void) seed;`.

Result: zero warnings across plain, ASAN+UBSAN, and TSAN builds
with the full project warning set.

## Verification

Local reproduction confirmed before and after:

```
# Before (off main): 3/27 TSAN tests fail with "data race"
cmake -B build-tsan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan -R 'http-echo|concurrency|keepalive'
# => FAIL: http-echo, concurrency-stress, property-keepalive

# After (this PR): 27/27 TSAN tests pass
ctest --test-dir build-tsan
# => 100% tests passed

# ASAN + UBSAN: 14/14 pass (no regression)
# Plain: 27/27 pass
```

## Why this matters

The TSAN CI job added in v0.5.15 was supposed to catch data races
in the library's own lock-free code (MPSC queue, atomic stats,
tracer PRNG). It couldn't do that job while its own test
infrastructure was racy — every PR was "red on TSAN" before any
library code was even considered. Now the TSAN signal is
meaningful: green means the library is race-free, not just that
the test infrastructure happens to not race this iteration.

This is also the right pattern for any future test that crosses
thread boundaries: declare shared state as atomics, use RELEASE
for writes that publish results, ACQUIRE for loads that observe
them, and let RELAXED carry the rest.

## Acceptance criteria
- [x] TSAN run: 27/27 pass, zero race reports.
- [x] ASAN+UBSAN run: zero regressions.
- [x] Plain run: 27/27 pass.
- [x] Zero compiler warnings across all three build configurations.
- [x] No `continue-on-error` softeners added; the TSAN job is now
      a real gate, not advisory.
