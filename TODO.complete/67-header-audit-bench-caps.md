# TODO 67 — Header accuracy audit + emit benchmark + cap overrides

**Status:** Complete (v0.5.27)
**Priority:** P1 (documentation accuracy) + P2 (performance tooling)

## What shipped (v0.5.27)

### 1. Stale header comments fixed

Three public/internal headers had stale claims that would mislead
contributors and callers:

**`include/otlp-c/metric.h`** (public API header):
- Said "Three metric types supported in v0.4" — four are supported
  in v0.5.x (Counter, Gauge, Histogram, ExponentialHistogram).
- Said "ExponentialHistogram and Summary are deferred" —
  ExponentialHistogram has been implemented since v0.5.x; only
  Summary is missing (the OTel spec itself recommends
  Histogram/ExpHistogram over Summary for new code).
- Said "Counter ... is_monotonic=true, cumulative temporality" —
  both became configurable in v0.5.26.

**`src/span_internal.h`** (internal header):
- Said "Span.Event — v0.5 supports name + time only; attributes
  are deferred" — events DO have attributes (up to
  `OTLP_EVENT_MAX_ATTRS`), implemented in v0.5.x.

These are the same class of issue fixed in v0.5.16 (CLAUDE.md
audit) and v0.5.19 (policy-docs audit) — stale claims from
earlier development phases that persisted because nobody re-read
the headers after the features shipped.

### 2. Compile-time span cap overrides

The span/event/link attribute caps were hardcoded `#define`s:
```c
#define OTLP_SPAN_MAX_ATTRIBUTES 128
#define OTLP_SPAN_MAX_EVENTS     64
#define OTLP_SPAN_MAX_LINKS      64
```

Callers who needed different limits had to modify the source.
Now guarded with `#ifndef`:
```c
#ifndef OTLP_SPAN_MAX_ATTRIBUTES
#define OTLP_SPAN_MAX_ATTRIBUTES 128
#endif
```

This is the standard C pattern for compile-time configurability.
Callers override via:
```sh
cmake -DCMAKE_C_FLAGS="-DOTLP_SPAN_MAX_ATTRIBUTES=256" ...
```

Defaults are unchanged. The guard prevents redefinition warnings
when the caller's build system pre-defines the macro.

Applied to all five caps:
- `OTLP_SPAN_MAX_ATTRIBUTES` (span.c)
- `OTLP_SPAN_MAX_EVENTS` (span.c)
- `OTLP_SPAN_MAX_LINKS` (span.c)
- `OTLP_EVENT_MAX_ATTRS` (span_internal.h)
- `OTLP_LINK_MAX_ATTRS` (span_internal.h)

### 3. Emit throughput benchmark

`bench/bench_emit.c` — measures the full emit pipeline:
- span clone (deep copy in `otlp_exporter_emit`)
- MPSC queue push
- tick drain into pending batch
- protobuf encode
- null_transport "send" (no HTTP, isolates library cost)

Runs 6 configurations (1000/5000 spans × 0/1/5/10 attrs).
Typical results on Apple silicon:
- 1000 spans, 0 attrs: ~29 μs/op, ~35K spans/sec
- 5000 spans, 5 attrs: ~25 μs/op, ~40K spans/sec

Registered as `otlp_bench_emit` in `bench/CMakeLists.txt`. Opt-in
via `-DOTLP_C_BUILD_BENCH=ON` (same as existing benchmarks).

### 4. Fixed bench_encode.c

`bench_encode.c` still used the v0.5.19 encoder signature
(without `resource_attributes` params added in v0.5.20). Fixed.
Also removed an unused helper function that triggered
`-Wunused-function`.

## Why this matters

**Stale headers undermine trust.** A caller reading metric.h to
decide whether the library supports ExponentialHistogram would
conclude "no, it's deferred" — even though it's fully
implemented. This is the exact failure mode that v0.5.16 fixed
for CLAUDE.md: the documentation says X, the code does Y, and
every reader is misled.

**Compile-time caps enable embedding.** Kernel modules and
firmware often have tight memory budgets. The default
`OTLP_SPAN_MAX_ATTRIBUTES = 128` means every span allocates
~6KB for the attribute array — even if the span has 0
attributes. With `#ifndef` guards, an embedded caller can set
`OTLP_SPAN_MAX_ATTRIBUTES=16` to cut per-span memory by 8×.

**The benchmark provides a perf baseline.** Before v0.5.27, the
library had no emit-pipeline benchmark. Optimization claims
were unverifiable. Now there's a reproducible number: ~35-40K
spans/sec through the full pipeline (clone + queue + encode +
send). Future optimizations can be measured against this.

## Acceptance criteria
- [x] metric.h comment reflects v0.5.x reality (4 types, configurable).
- [x] span_internal.h event comment reflects attribute support.
- [x] All 5 span/event/link caps have `#ifndef` guards.
- [x] Defaults unchanged (128/64/64/32/32).
- [x] bench_emit.c builds and runs.
- [x] bench_encode.c compiles (stale encoder call fixed).
- [x] Zero warnings across plain/TSAN/ASAN builds.
- [x] 32/32 tests pass.
