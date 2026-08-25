/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Portability shims shared by the POSIX test binaries. Test-only —
 * the library proper has its own portable layers (platform.c,
 * http_response_parser.c).
 *
 * otlp_test_memmem is always a local byte-search: libc memmem's
 * declaration is feature-macro-gated differently on every platform
 * (FreeBSD hides it under strict _POSIX_C_SOURCE, MSVC lacks it
 * entirely), so the tests run one deterministic code path
 * everywhere instead of three per-platform fallbacks.
 *
 * OTLP_TEST_INADDR_LOOPBACK spells INADDR_LOOPBACK out: FreeBSD's
 * netinet/in.h hides the macro whenever _POSIX_C_SOURCE is defined
 * (the build defines it globally, for CLOCK_MONOTONIC), and the
 * value — 127.0.0.1 — is fixed by POSIX anyway.
 */
#ifndef OTLP_TEST_PORTABLE_H
#define OTLP_TEST_PORTABLE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define OTLP_TEST_INADDR_LOOPBACK 0x7f000001U

static inline void *
otlp_test_memmem(const void *hay,
	size_t hay_len,
	const void *ndl,
	size_t ndl_len)
{
	const uint8_t *h = hay;
	size_t i;

	if (ndl_len == 0 || hay_len < ndl_len)
		return NULL;
	for (i = 0; i <= hay_len - ndl_len; i++)
		if (memcmp(h + i, ndl, ndl_len) == 0)
			return (void *) (h + i);
	return NULL;
}

#endif /* OTLP_TEST_PORTABLE_H */
