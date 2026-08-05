# TODO 09 — Fix MSVC atomics without _HAS_C11_ATOMICS hack

**Status:** Ready
**Priority:** P0
**Depends on:** nothing

## Problem

`<stdatomic.h>` on MSVC fails with `C1189: "C atomic support is not enabled"`.
Current workaround forces `_HAS_C11_ATOMICS=1` via compile definition, which
bypasses the installation check but is fragile — it depends on the VS
installation actually shipping the atomic support libraries.

## Proper fix

Replace `_Atomic` types in `src/tracer.c` and `src/mpsc_queue.c` with
MSVC-compatible intrinsics on Windows:

```c
#ifdef _MSC_VER
    /* MSVC: use _InterlockedCompareExchange64, _InterlockedExchange64 */
    #include <intrin.h>
    #define OTLP_ATOMIC_LOAD(p)        (*(p))
    #define OTLP_ATOMIC_CAS(p, o, n)   (_InterlockedCompareExchange64((long volatile*)(p), (n), (o)) == (o))
    #define OTLP_ATOMIC_STORE(p, v)    (*(p)) = (v)
#else
    /* GCC/Clang: use C11 <stdatomic.h> */
    #include <stdatomic.h>
    #define OTLP_ATOMIC_LOAD(p)        atomic_load(p)
    #define OTLP_ATOMIC_CAS(p, o, n)   atomic_compare_exchange_strong(p, &(o), (n))
    #define OTLP_ATOMIC_STORE(p, v)    atomic_store(p, v)
#endif
```

This removes the `<stdatomic.h>` dependency on MSVC entirely, eliminating
the `_HAS_C11_ATOMICS` hack and the VS component installation requirement.

## Acceptance criteria

- [ ] Windows MSVC builds without `_HAS_C11_ATOMICS` define.
- [ ] All 13 tests pass on Windows.
- [ ] No regression on GCC/Clang.
