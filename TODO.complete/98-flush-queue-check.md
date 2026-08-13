# TODO 98 — Flush return-status includes MPSC queue sizes

**Status:** Complete (v0.5.58)
**Priority:** P1 (correctness: silent data loss)

## What shipped

`otlp_exporter_flush`'s return-status check was missing
`mpsc_queue_size` for the three signals. The loop condition
included queue sizes; the return-status check did not.

If the deadline was reached with items still in MPSC queues
(e.g., drain cap hit, or a tight race after a POST completion
cleared pending but before the next drain), flush silently
returned `OTLP_OK` with unsent items.

## Sites changed

- `src/exporter.c::otlp_exporter_flush` — return-status check
  now includes `mpsc_queue_size(&e->{queue,metric_queue,log_queue})`.

## When the bug manifested

The race window is narrow but reachable:

1. User emits many items (queue fills beyond the drain cap of
   `batch_size * 2`).
2. User calls `flush()` with a bounded `flush_timeout_ms`.
3. The POST in flight completes, clearing pending.
4. The next drain hasn't run yet.
5. flush's deadline hits.
6. Return check sees pending=0, in_flight=NULL — returns OK.
7. User frees exporter. Items in queue are dropped.

With the fix, step 6 sees queue_size > 0 and returns
`OTLP_ERR_NETWORK`. The user can retry or accept the loss
explicitly.

## Why no regression test

The race is timing-dependent: requires the flush deadline to hit
in the narrow window between POST completion and the next drain.
Reliable reproduction would require either:
- A custom allocator hook that injects delays (intrusive).
- A test-only knob to pause the tick loop mid-iteration
  (invasive).

The fix is correct by inspection — the return check now matches
the loop condition exactly.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 34/34 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

## Audit context

This bug was found by reading the flush function carefully and
noticing the asymmetry between the loop condition and the return
check. Same shape as previous "missing condition" bugs:

- v0.5.51: slab double-free path missed the "pointer in arena"
  check before calling free().
- v0.5.55: exporter_create resource_attributes cleanup missed
  the zero-init requirement.
- v0.5.56: exporter_create fail path missed mpsc_queue_free
  calls.
- v0.5.58: flush return-status missed queue-size checks.

All are "function does X in one place but a slightly different X
in another place" patterns. The fix is to make the two places
identical.

## Next likely targets

- The flush_metric / flush_log paths (still inline encode +
  sync POST). Could have similar asymmetric checks.
- The HTTP response parser's Transfer-Encoding handling (still
  unhandled; chunked responses would be misparsed but the body
  isn't interpreted so it's benign for OTLP).
- The tracer PRNG seeding (low priority per OTLP spec).
