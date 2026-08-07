/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Version constants. Bumped in lockstep with CMakeLists.txt and
 * vcpkg.json. See docs/roadmap.md for the version policy.
 *
 * ABI stability promise:
 *   - MAJOR bumps on incompatible API changes.
 *   - MINOR bumps on backwards-compatible feature additions.
 *   - PATCH bumps on backwards-compatible bug fixes.
 *
 * Within the 0.x line, the API is unstable.
 */
#ifndef OTLP_C_VERSION_H
#define OTLP_C_VERSION_H

#define OTLP_C_VERSION_MAJOR 0
#define OTLP_C_VERSION_MINOR 5
#define OTLP_C_VERSION_PATCH 11
#define OTLP_C_VERSION_STRING "0.5.11"

#define OTLP_C_VERSION                                                            \
	(((uint32_t)OTLP_C_VERSION_MAJOR << 24) |                                 \
	 ((uint32_t)OTLP_C_VERSION_MINOR << 16) |                                 \
	 ((uint32_t)OTLP_C_VERSION_PATCH))

#include <stdint.h>

#endif
