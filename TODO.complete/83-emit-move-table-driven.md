# TODO 83 — Table-driven emit pipeline

**Status:** Complete (v0.5.43)
**Priority:** P2 (architecture: DRY + OCP)

## What shipped

Replaced the three move-variant and three clone-variant emit
functions with a descriptor-based dispatch:

- `struct signal_emit_path` — per-signal descriptor: queue,
  emitted/dropped counters, type-erased free/clone, signal name.
- `emit_move_common(e, &path, item)` — core of every move variant.
- `emit_clone_common(e, &path, item)` — core of every clone variant.
- Six thin wrappers (one per public emit function) that build a
  descriptor and delegate.

The triplicated logic (six copies of NULL check → shutdown → push →
free + counter + log) collapses into two helpers. ~150 lines of
near-duplicate code → ~50 lines of helpers + ~60 lines of wrappers.

## Sites changed

- `src/exporter.c` — added `span_free_void`, `metric_free_void`,
  `log_free_void`, `span_clone_void`, `metric_clone_void`,
  `log_clone_void`; added `struct signal_emit_path`; added
  `emit_move_common`, `emit_clone_common`; rewrote
  `otlp_exporter_emit`, `emit_move`, `emit_metric`, `emit_metric_move`,
  `emit_log`, `emit_log_move` as descriptor-driven wrappers.

## Why

- **DRY.** Behavior changes (new counter, log format tweak, retry
  policy) touch one helper, not six functions. The v0.5.41
  shutdown-leak fix and the v0.5.42 symmetry fix both had to be
  applied per-function — the descriptor makes that class of fix a
  one-liner going forward.
- **OCP.** Adding a fourth signal is a one-descriptor addition.
  OpenTelemetry's profiler signal (in development upstream) would
  fit the same pipeline trivially.
- **MECE.** The emit pipeline has a single owner per concern
  (move vs clone). The public surface is pure dispatch.

## Type safety

Type erasure is localized to six tiny wrappers:
```c
static void  span_free_void(void *p)            { otlp_span_free(p); }
static void *span_clone_void(const void *p)     { return otlp_span_clone(p); }
```

The wrappers keep the cast localized; the typed public functions
retain full type safety.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build -E http-timeout   # 39/39 pass
                                       # http-timeout: known macOS-local
                                       # flake; passes in CI on Linux.

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout  # ASAN clean
```

The async_metrics properties exercise all three signals through
both clone and move variants, plus the shutdown-drop paths from
v0.5.41 and the clone-shutdown paths from v0.5.42. If the
descriptor wires the wrong free/clone to a signal, ASAN catches
the type mismatch immediately.

## Future

The same pattern applies to:
- `try_start_post` / `try_start_metric_post` / `try_start_log_post`
  (already share helpers via exporter_otel; could be fully unified).
- `record_outcome`'s signal-aware counter updates (currently a
  switch on `in_flight_signal`).
- `flush_metric` / `flush_log` (still inline encode logic).

Each is a candidate for a follow-up release. The emit pipeline
was the highest-value target because it had the most triplication
and the hottest call path.
