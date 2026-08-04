/* SPDX-License-Identifier: Apache-2.0 */
/*
 * otlp-c — umbrella public header. Include this to get everything
 * the typical caller needs. Power users can include the specific
 * sub-headers (span.h, exporter.h, etc.) individually.
 *
 * Public API stability: stable within a major version. The 0.x
 * line may break between minor versions; document changes in
 * CHANGELOG.
 */
#ifndef OTLP_C_H
#define OTLP_C_H

#include "version.h"
#include "status.h"
#include "span.h"
#include "tracer.h"
#include "exporter.h"
#include "visibility.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Returns the runtime version string. The same as
 * OTLP_C_VERSION_STRING, but accessible to dynamic callers. */
OTLP_C_EXPORT
const char *otlp_version(void);

#ifdef __cplusplus
}
#endif

#endif
