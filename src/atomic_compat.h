/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Atomic operations compatibility shim.
 *
 * Wraps the small subset of C11 `<stdatomic.h>` we use (atomic_load,
 * atomic_store, atomic_compare_exchange_weak on uint64_t) so the
 * library compiles on MSVC, whose vcruntime `<stdatomic.h>` is
 * unreliable across VS preview versions.
 *
 * On GCC/Clang, this is a thin pass-through to `<stdatomic.h>` so
 * we get optimal code generation and the standard memory model.
 *
 * On MSVC, we use compiler intrinsics (`_InterlockedCompareExchange64`,
 * `_InterlockedExchange64`). These have full barriers by default,
 * which is slightly stronger than the C11 orders we ask for, but
 * correct — the strong order is a no-op for our lock-free queue
 * correctness.
 *
 * Scope: uint64_t only. The library uses no other atomic types.
 */
#ifndef OTLP_C_ATOMIC_COMPAT_H
#define OTLP_C_ATOMIC_COMPAT_H

#include <stdint.h>

#if defined(_MSC_VER)

#include <intrin.h>

typedef volatile uint64_t otlp_atomic_u64;

#define OTLP_MEMORY_ORDER_RELAXED 0
#define OTLP_MEMORY_ORDER_ACQUIRE 1
#define OTLP_MEMORY_ORDER_RELEASE 2

static inline uint64_t
otlp_atomic_load_explicit(otlp_atomic_u64 *a, int mo)
{
	(void) mo;
	/* Aligned volatile read of 64 bits is atomic on x64/ARM64. */
	return *a;
}

static inline void
otlp_atomic_store_explicit(otlp_atomic_u64 *a, uint64_t v, int mo)
{
	(void) mo;
	_InterlockedExchange64((long long *) a, (long long) v);
}

static inline int
otlp_atomic_cas_weak_explicit(otlp_atomic_u64 *a,
			      uint64_t	       *expected,
			      uint64_t		desired,
			      int			succ,
			      int			fail)
{
	uint64_t old;

	(void) succ;
	(void) fail;
	old = (uint64_t) _InterlockedCompareExchange64(
	    (long long *) a, (long long) desired, (long long) *expected);
	if (old == *expected)
		return 1;
	*expected = old;
	return 0;
}

#else

#include <stdatomic.h>

typedef _Atomic uint64_t otlp_atomic_u64;

#define OTLP_MEMORY_ORDER_RELAXED memory_order_relaxed
#define OTLP_MEMORY_ORDER_ACQUIRE memory_order_acquire
#define OTLP_MEMORY_ORDER_RELEASE memory_order_release

static inline uint64_t
otlp_atomic_load_explicit(otlp_atomic_u64 *a, int mo)
{
	return atomic_load_explicit(a, mo);
}

static inline void
otlp_atomic_store_explicit(otlp_atomic_u64 *a, uint64_t v, int mo)
{
	atomic_store_explicit(a, v, mo);
}

static inline int
otlp_atomic_cas_weak_explicit(otlp_atomic_u64 *a,
			      uint64_t	       *expected,
			      uint64_t		desired,
			      int			succ,
			      int			fail)
{
	return atomic_compare_exchange_weak_explicit(
	    a, expected, desired, succ, fail);
}

#endif

#endif
