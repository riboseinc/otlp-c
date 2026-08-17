/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Exporter internals — internal-only header for tests. NOT
 * installed. The public exporter API remains the surface in
 * include/otlp-c/exporter.h.
 */
#ifndef OTLP_C_EXPORTER_INTERNAL_H
#define OTLP_C_EXPORTER_INTERNAL_H

#include <otlp-c/exporter.h>

#include <stddef.h>

/* Test-only: the exporter's copy of opts.resource_attributes AFTER
 * map-semantics normalization (duplicate keys collapsed
 * last-write-wins; "service.name" dropped when the dedicated
 * service_name opt is set). Points into the exporter; valid until
 * otlp_exporter_free(). */
const otlp_resource_attr_t *
otlp_exporter_get_resource_attrs(const otlp_exporter_t *e, size_t *n);

#endif
