# TODO 84 — Table-driven record_outcome

**Status:** Complete (v0.5.44)
**Priority:** P2 (architecture: DRY + OCP, follow-up to v0.5.43)

## What shipped

`record_outcome` and its three helpers
(`clear_in_flight_batch`, `add_sent_for_signal`,
`add_dropped_err_for_signal`) each had a switch on
`e->in_flight_signal` with three cases. Three switches × three
cases = 9-way dispatch spread across four functions. Adding a
new signal meant touching all four.

Replaced with a descriptor pattern mirroring v0.5.43's emit
descriptor:

- `struct signal_record_path` — per-signal record-outcome
  descriptor: pending array (type-erased), pending_count pointer,
  first_set pointer, `free_item` fn, sent_counter,
  dropped_err_counter, signal_name.
- `record_path_for(e)` — looks up the descriptor. Single switch.
- Three helpers now take `const struct signal_record_path *` and
  don't switch internally.
- `record_outcome` builds the descriptor once at the top and
  passes it to each helper. The signal-name ternary at the top
  is gone.

## Sites changed

- `src/exporter.c`:
  - Added `struct signal_record_path`.
  - Added `record_path_for`.
  - Rewrote `clear_in_flight_batch`, `add_sent_for_signal`,
    `add_dropped_err_for_signal` to take descriptor.
  - Rewrote `record_outcome` to build descriptor once and pass it.

## Why

- **DRY.** Adding a 4th signal is one case in `record_path_for`
  plus one initializer. The helpers and `record_outcome` body
  don't change.
- **MECE.** Signal dispatch lives in exactly one place. Helpers
  are signal-agnostic.
- **Type safety.** Type erasure is isolated to `free_item` (the
  `*_void` wrappers from v0.5.43).

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 39/39 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

The async_metrics properties exercise all three signals through
the record_outcome path (send / drop / retry / shutdown-drop).
A miswired descriptor would surface as ASAN type-mismatch or
wrong-counter- incremented assertions in the property checks.

## Future

The `free_pending_batch` helper (used only by `exporter_free`)
was left as-is — it's not on the record_outcome path. A future
release could extend the descriptor pattern to the free path
for full consistency.

The three signal-aware "path" structs (signal_path for tick,
signal_emit_path for emit, signal_record_path for record_outcome)
could potentially be unified into one master descriptor. That's
premature — they have disjoint field sets and the current
separation keeps each focused.
