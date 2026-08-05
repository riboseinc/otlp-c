# ADR 0005 — C11 atomics only for the MPSC queue

## Status

Accepted, 2026-08-05. Encoded in `CMakeLists.txt`: `CMAKE_C_STANDARD 11`.

## Context

`CLAUDE.md` says "pure C99 baseline." C99 has no atomics. We need
atomics for the MPSC queue (and the per-stat counters). The
historically portable solution is platform intrinsics (`__atomic_*`
on GCC, `_Interlocked*` on MSVC, `__sync_*` legacy), which is
inconsistent and harder to reason about.

## Decision

Use C11 `<stdatomic.h>` and bump the build to C11. The C11 standard
is supported by GCC 4.9+, Clang 3.8+, MSVC 19.27+ (under
`/std:c11`). All current toolchains default to C11 or later.

The only C11 feature we use is `<stdatomic.h>` and the `_Atomic`
keyword. Everything else stays C99-style (no anonymous structs,
no `_Generic`, no designated initializers).

For MSVC, `<stdatomic.h>` requires `/Za` not to be passed (i.e.
language extensions must be enabled). We set `CMAKE_C_EXTENSIONS ON`
on MSVC. This is the only platform-specific compile-time adjustment.

## Alternatives considered

1. **GCC intrinsics + MSVC fallback.** Doubles the implementation;
  the MPSC queue needs cross-platform atomic CAS, memory orders,
  and the surface area is large. *Rejected.*
2. **Open-addressed portable atomics library (libatomic_ops).**
  Adds a dependency. *Rejected.*
3. **Pure C99 with mutex-protected state.** Violates ADR 0002.
  *Rejected.*

## Consequences

- (+) Single source of truth for atomics.
- (+) Compiler-builtin optimizations (e.g. on x86, C11 atomics
  compile down to plain loads/stores with no fences since the
  hardware TSO).
- (-) Bumps baseline from C99 to C11. Acceptable for v0.x given
  toolchain support.
- (-) MSVC needs `/Za` to be off. Documented in `CMakeLists.txt`.

## When this changes

If a future C standard adds a new atomic feature we need (e.g.
wait-free atomics), revisit. C11 atomics are stable for the
MPSC use case.
