# TODO 96 — Fail-injecting allocator test + mpsc_queue cleanup leak

**Status:** Complete (v0.5.56)
**Priority:** P0 (memory safety: leak found via new test) + P2 (test infrastructure)

## What shipped

Two related changes bundled:

1. **Fail-injecting allocator test infrastructure.** New
   `test_allocator_oom.c` iterates a "fail at Nth alloc" probe
   over an operation. Each iteration: install fail allocator,
   run op, free result, assert `alloc_count == free_count`.

2. **mpsc_queue cleanup leak in exporter_create fail path.** The
   new test caught a real bug: `otlp_exporter_create` freed
   user_agent, service_name, resource_attributes (and contents),
   pending arrays, and the exporter struct in the fail path —
   but NOT the three mpsc_queue slots. If any `mpsc_queue_init`
   succeeded before the failure, those slots leaked.

## Why this is the third bug of the same class

The audit pattern keeps finding partial-init cleanup bugs:
- v0.5.47: `otlp_attribute_copy_all` fail path freed 0..i-1 but
  not the failed item i (leaked key).
- v0.5.55: `exporter_create` resource_attributes used
  `otlp_malloc` so cleanup iteration hit uninitialized garbage
  (UB).
- **v0.5.56: `exporter_create` fail path missed the mpsc_queue
  slots entirely (leaked).**

All three are the same shape: a multi-resource init function
where the fail path doesn't free every resource that may have
been allocated.

## Sites changed

- `src/exporter.c::otlp_exporter_create` fail path — added
  three `mpsc_queue_free` calls.
- `tests/test_allocator_oom.c` — new test file.
- `tests/CMakeLists.txt` — register `allocator-oom` test.

## How the test works

```c
static int alloc_count = 0;
static int free_count  = 0;
static int fail_at     = -1;

static void *fail_alloc(size_t n) {
    /* Pre-check: would this be the fail_at-th alloc? */
    if (fail_at > 0 && alloc_count + 1 >= fail_at)
        return NULL;  /* don't increment — nothing to free */
    alloc_count++;
    return malloc(n);
}

for (int n = 1; n <= 60; n++) {
    reset_counters(n);
    exp = otlp_exporter_create(&opts);
    if (exp) otlp_exporter_free(exp);
    assert(alloc_count == free_count);
}
```

Key design: failed allocs don't increment the counter. The leak
check then reduces to "every successful alloc must be paired
with a free."

## What the test covers

- `test_exporter_create_oom`: 60 iterations probing every alloc
  offset in exporter_create. Catches the mpsc_queue leak
  regression (v0.5.56) AND any future partial-init bug in the
  constructor.

- `test_span_clone_oom`: 50 iterations probing every alloc
  offset in span_clone. Confirms v0.5.47's attribute_copy_all
  fix is correct (previously correct-by-inspection only).

## What the test does NOT cover

- realloc failures (the fail allocator handles realloc but
  library code rarely reallocs during init).
- Failures inside the library's steady-state (only init paths
  are exercised).
- Concurrent alloc failures (test is single-threaded).

These gaps are acceptable for v0.5.56; future work could extend
the harness.

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

Continues the partial-init cleanup audit:
- v0.5.47: attribute_copy_all.
- v0.5.55: resource_attributes zero-init.
- v0.5.56: mpsc_queue slots in fail path + test infrastructure.

The fail-injecting allocator closes the testability gap for
this entire bug class. Future partial-init bugs in init paths
will be caught automatically by extending test_allocator_oom.c.
