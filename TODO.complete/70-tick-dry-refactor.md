# TODO 70 — tick() DRY refactor (table-driven signal dispatch)

**Status:** Complete (v0.5.30)
**Priority:** P1 (code quality — DRY)

## What shipped (v0.5.30)

### Problem: signal triplication in tick()

v0.5.28 (TODO 68) added async metric/log batching to tick().
The implementation added metric/log handling as THREE PARALLEL
code blocks alongside the existing span block:

| Section | Lines (v0.5.28) | Copies |
|---|---|---|
| Drain queue → pending | ~12 | 3 |
| Null-transport check | ~15 | 3 |
| Batch-ready + POST start | ~15 | 3 |
| Backoff retry dispatch | ~10 | 3 (switch) |

Total: ~52 lines of triplicated logic. Each copy differed only in
which queue, pending array, timer field, and start_post function
to use — a classic DRY violation.

### Fix: struct signal_path descriptor table

Introduced a descriptor struct that bundles per-signal state:

```c
struct signal_path {
    struct mpsc_queue      *queue;
    void                  **pending;
    size_t                  pending_cap;
    size_t                 *pending_count;
    bool                   *first_set;
    uint64_t               *first_mono;
    int                     signal_kind;
    otlp_status_t         (*start_post)(struct otlp_exporter *e);
};
```

tick() builds `paths[3]` at function entry (all fields point INTO
the exporter struct, so mutations through paths[] update e
directly). Then:

**Drain** — one `for (s = 0; s < 3; s++)` loop:
```c
for (s = 0; s < 3; s++)
    while (*paths[s].pending_count < paths[s].pending_cap) {
        void *item = mpsc_queue_pop(paths[s].queue);
        ...
        paths[s].pending[(*paths[s].pending_count)++] = item;
        ...
    }
```

**Null-transport** — one loop by priority:
```c
for (s = 0; s < 3; s++)
    if (*paths[s].pending_count > 0) {
        e->in_flight_signal = paths[s].signal_kind;
        record_outcome(e, http_status);
        goto tick_continue;
    }
```

**POST start** — one loop with batch-ready check:
```c
for (s = 0; s < 3; s++) {
    if (e->in_flight || e->backoff_armed) break;
    if (*paths[s].pending_count >= batch_size || ...)
        paths[s].start_post(e);
}
```

**Backoff retry** — table lookup (no switch):
```c
paths[e->in_flight_signal].start_post(e);
```

### Benefits

1. **DRY**: ~80 lines of triplicated code → ~30 lines of looped
   code. Net reduction of ~50 lines.
2. **OCP**: adding a fourth signal (e.g., a future "events"
   signal) is one `paths[]` entry, not another parallel block.
3. **Readability**: tick() is shorter and the signal-handling
   pattern is visible at a glance.
4. **Maintainability**: a bug fix in the drain/batch/null-transport
   logic applies to ALL signals automatically (one loop body, not
   three).

### Void** casts

The pending arrays are typed (`otlp_span_t **`, etc.) but cast to
`void **` for the paths[] descriptor. In C, all object pointer
types have the same representation; `void *` interconverts with
any object pointer. The cast is safe and portable. TSAN/ASAN
confirm no aliasing issues.

### Behavior-preserving

The refactor is mechanical — same operations, same order, same
semantics. All 33 tests pass unchanged (plain, TSAN, ASAN+UBSAN).

## Acceptance criteria
- [x] struct signal_path defined with all per-signal fields.
- [x] tick() builds paths[3] from exporter fields.
- [x] Drain: one loop replaces three.
- [x] Null-transport: one loop replaces three.
- [x] POST start: one loop replaces three.
- [x] Backoff retry: table lookup replaces switch.
- [x] 33/33 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
- [x] No behavioral change (behavior-preserving refactor).
