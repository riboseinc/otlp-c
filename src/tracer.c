/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Tracer — span factory with random ID generation.
 *
 * The tracer owns a per-instance PRNG seeded from a mix of the
 * monotonic clock, the calling thread's id, and the process id.
 * The PRNG state is a single atomic word updated via CAS, so
 * otlp_tracer_start_span is safe to call from multiple threads
 * without locks.
 *
 * service_name / scope_name / scope_version are stored verbatim
 * (copied on create); they are NOT attached to individual spans —
 * the exporter reads them from the tracer at emit time and writes
 * them into the ResourceSpans envelope. This keeps the span
 * allocation small and avoids duplicating these strings per span.
 */
#include <otlp-c/tracer.h>

#include "internal_util.h"
#include "platform.h"
#include "span_internal.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

struct otlp_tracer
{
	char *service_name; /* owned */
	char *scope_name; /* owned */
	char *scope_version; /* owned */
	_Atomic uint64_t prng_state;
};

/* ── Internal helpers ─────────────────────────────────────────── */

static uint64_t
get_thread_id(void)
{
#if defined(_WIN32)
	return (uint64_t) GetCurrentThreadId();
#else
	/* pthread_self() returns an opaque pthread_t. Cast via
	 * uintptr_t to get a deterministic numeric value. */
	return (uint64_t) (uintptr_t) pthread_self();
#endif
}

static uint64_t
get_pid(void)
{
#if defined(_WIN32)
	return (uint64_t) GetCurrentProcessId();
#else
	return (uint64_t) getpid();
#endif
}

/* xorshift64s — scrambler improves statistical quality over plain
 * xorshift64. State must be non-zero. */
static uint64_t
xorshift64s(uint64_t s)
{
	s ^= s << 13;
	s ^= s >> 7;
	s ^= s << 17;
	return s * 0x2545F4914F6CDD1DULL;
}

/* Advance the per-tracer atomic PRNG state. Lock-free via CAS. */
static uint64_t
tracer_prng_next(struct otlp_tracer *t)
{
	uint64_t old;
	uint64_t new;

	old = atomic_load_explicit(&t->prng_state, memory_order_relaxed);
	do
	{
		new = xorshift64s(old);
	} while (!atomic_compare_exchange_weak_explicit(&t->prng_state,
		&old,
		new,
		memory_order_relaxed,
		memory_order_relaxed));
	return new;
}

/* Big-endian byte packing for portable trace/span IDs (so the same
 * uint64 produces the same byte sequence on any platform). */
static void
put_u64_be(uint8_t out[8], uint64_t v)
{
	out[0] = (uint8_t) (v >> 56);
	out[1] = (uint8_t) (v >> 48);
	out[2] = (uint8_t) (v >> 40);
	out[3] = (uint8_t) (v >> 32);
	out[4] = (uint8_t) (v >> 24);
	out[5] = (uint8_t) (v >> 16);
	out[6] = (uint8_t) (v >> 8);
	out[7] = (uint8_t) v;
}

/* Fill `out` with `len` random bytes (len must be a multiple of 8).
 * Rejects an all-zero result and regenerates (a zero span_id is the
 * only invalid value per OTLP). */
static void
fill_random_bytes(struct otlp_tracer *t, uint8_t *out, size_t len)
{
	size_t off = 0;

	while (off < len)
	{
		uint64_t v = tracer_prng_next(t);
		put_u64_be(out + off, v);
		off += 8;
	}

	/* Reject all-zero (extremely unlikely: 2^-64 for span_id). */
	if (len > 0)
	{
		bool all_zero = true;
		for (size_t i = 0; i < len; i++)
		{
			if (out[i] != 0)
			{
				all_zero = false;
				break;
			}
		}
		if (all_zero)
		{
			/* Recurse with a fresh pull. */
			fill_random_bytes(t, out, len);
		}
	}
}

/* ── Lifecycle ────────────────────────────────────────────────── */

otlp_tracer_t *
otlp_tracer_create(const char *service_name,
	const char *scope_name,
	const char *scope_version)
{
	struct otlp_tracer *t;
	uint64_t mono = 0;
	uint64_t seed;

	t = malloc(sizeof(*t));
	if (!t)
		return NULL;
	memset(t, 0, sizeof(*t));

	t->service_name = otlp_dup_str(service_name ? service_name : "");
	t->scope_name = otlp_dup_str(scope_name ? scope_name : "");
	t->scope_version = otlp_dup_str(scope_version ? scope_version : "");
	if (!t->service_name || !t->scope_name || !t->scope_version)
	{
		free(t->service_name);
		free(t->scope_name);
		free(t->scope_version);
		free(t);
		return NULL;
	}

	/* Seed: mono clock ^ thread id ^ pid. Fallback to a fixed
	 * constant if all three happen to be zero (impossible in
	 * practice but defensive). */
	(void) otlp_platform_now_mono_nano(&mono);
	seed = mono ^ get_thread_id() ^ get_pid();
	if (seed == 0)
		seed = 0x9E3779B97F4A7C15ULL;
	atomic_store_explicit(&t->prng_state, seed, memory_order_relaxed);
	return t;
}

void
otlp_tracer_free(otlp_tracer_t *tracer)
{
	if (!tracer)
		return;
	free(tracer->service_name);
	free(tracer->scope_name);
	free(tracer->scope_version);
	free(tracer);
}

/* ── Span creation ────────────────────────────────────────────── */

static otlp_span_t *
start_span_internal(struct otlp_tracer *t,
	const char *name,
	const otlp_span_t *parent)
{
	otlp_span_t *span;

	if (!t)
		return NULL;

	span = otlp_span_create(name);
	if (!span)
		return NULL;

	if (parent)
	{
		/* Child: inherit trace ID, set parent link. */
		const uint8_t *p_trace = otlp_span_get_trace_id(parent);
		const uint8_t *p_span = otlp_span_get_span_id(parent);

		memcpy((uint8_t *) otlp_span_get_trace_id(span),
			p_trace,
			OTLP_TRACE_ID_LEN);
		/* Cast away const for the internal write; the public
		 * setter would also work but does bounds-checking we
		 * don't need here. */
		otlp_span_set_parent_span_id(span, p_span);
	}
	else
	{
		/* Root: fresh trace ID. */
		fill_random_bytes(t,
			(uint8_t *) otlp_span_get_trace_id(span),
			OTLP_TRACE_ID_LEN);
	}

	/* Always: fresh span ID. */
	fill_random_bytes(
		t, (uint8_t *) otlp_span_get_span_id(span), OTLP_SPAN_ID_LEN);

	otlp_span_mark_start(span);
	return span;
}

otlp_span_t *
otlp_tracer_start_span(otlp_tracer_t *tracer, const char *name)
{
	return start_span_internal(tracer, name, NULL);
}

otlp_span_t *
otlp_tracer_start_child_span(otlp_tracer_t *tracer,
	const char *name,
	const otlp_span_t *parent)
{
	if (!parent)
		return NULL;
	return start_span_internal(tracer, name, parent);
}
