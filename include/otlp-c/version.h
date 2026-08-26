/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Version constants. Bumped in lockstep with CMakeLists.txt and
 * vcpkg.json. See docs/roadmap.md for the version policy.
 *
 * Stability promise (since 1.0.0):
 *   - MAJOR bumps on incompatible API changes.
 *   - MINOR bumps on backwards-compatible feature additions.
 *   - PATCH bumps on backwards-compatible bug fixes.
 *
 * The 1.x line freezes the SOURCE API: no breaking changes to
 * signatures, types, or semantics — additions only. The C ABI
 * (struct layouts, symbol set) is NOT guaranteed across builds;
 * consumers compile against the headers.
 */
#ifndef OTLP_C_VERSION_H
#define OTLP_C_VERSION_H

#include "visibility.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define OTLP_C_VERSION_MAJOR 1
#define OTLP_C_VERSION_MINOR 1
#define OTLP_C_VERSION_PATCH 5

/* Derived from the numeric macros so the string can never drift
 * (it was a hand-maintained literal until v0.5.88 — stuck at
 * "0.5.68" for nineteen releases while the numbers were bumped). */
#define OTLP_C_STR2(x) #x
#define OTLP_C_STR(x) OTLP_C_STR2(x)
#define OTLP_C_VERSION_STRING                                \
	OTLP_C_STR(OTLP_C_VERSION_MAJOR)                     \
	"." OTLP_C_STR(OTLP_C_VERSION_MINOR) "." OTLP_C_STR( \
		OTLP_C_VERSION_PATCH)

#define OTLP_C_VERSION                                    \
	(((uint32_t) OTLP_C_VERSION_MAJOR << 24) |        \
		((uint32_t) OTLP_C_VERSION_MINOR << 16) | \
		((uint32_t) OTLP_C_VERSION_PATCH))

	/* Returns the runtime version string. Same as OTLP_C_VERSION_STRING
	 * but accessible to dynamic callers. */
	OTLP_C_EXPORT
	const char *otlp_version(void);

#ifdef __cplusplus
}
#endif

#endif
