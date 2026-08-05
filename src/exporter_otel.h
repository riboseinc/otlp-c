/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Exporter-internal: build an HTTP request from a batch of spans.
 * The exporter (src/exporter.c) drives the resulting request via
 * otlp_http_request_step.
 *
 * Encoding happens once at build time. Retries re-use the encoded
 * body (no re-encode per attempt).
 */
#ifndef OTLP_C_EXPORTER_OTEL_H
#define OTLP_C_EXPORTER_OTEL_H

#include <otlp-c/exporter.h>
#include <otlp-c/span.h>

#include "http_client.h"

#include <stddef.h>

/* Build an HTTP POST request from `spans`. The request is in
 * CONNECTING state on return. The encoded body is owned by the
 * request; the caller frees it via otlp_http_request_free.
 *
 * `service_name` and `user_agent` are passed verbatim; NULL means
 * "use the protocol default." */
otlp_status_t
otlp_exporter_otel_build_request(const struct otlp_http_url *url,
	const char *user_agent,
	const char *service_name,
	const otlp_span_t *const *spans,
	size_t n_spans,
	otlp_http_request_t **out);

#endif
