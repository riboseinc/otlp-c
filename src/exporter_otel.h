/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Exporter-internal: build HTTP requests from batches of items.
 * The exporter (src/exporter.c) drives the resulting requests via
 * otlp_http_request_step.
 *
 * One builder per signal: spans, metrics, logs. Each handles
 * pre-sizing the encode buffer, encoding the corresponding
 * Export*ServiceRequest, and starting the HTTP request (reusing a
 * keep-alive socket if provided).
 *
 * Encoding happens once at build time. Retries re-use the encoded
 * body (no re-encode per attempt).
 */
#ifndef OTLP_C_EXPORTER_OTEL_H
#define OTLP_C_EXPORTER_OTEL_H

#include <otlp-c/exporter.h>
#include <otlp-c/log.h>
#include <otlp-c/metric.h>
#include <otlp-c/span.h>

#include "http_client.h"

#include <stddef.h>

/* Build an HTTP POST request for a batch of `spans`. The request is
 * in CONNECTING state on return (or SENDING if `reuse_socket` is
 * provided). The encoded body is owned by the request; the caller
 * frees it via otlp_http_request_free.
 *
 * `service_name` and `user_agent` are passed verbatim; NULL means
 * "use the protocol default."
 *
 * If `reuse_socket` is non-NULL, it must be a previously-detached
 * keep-alive socket from a successful prior request. The new request
 * takes ownership and will close it on _free (unless re-detached).
 * Pass NULL to open a fresh connection. */
otlp_status_t
otlp_exporter_otel_build_span_request(const struct otlp_http_url *url,
	const char *user_agent,
	const char *service_name,
	const otlp_resource_attr_t *resource_attributes,
	size_t n_resource_attributes,
	const otlp_span_t *const *spans,
	size_t n_spans,
	uint32_t connect_timeout_ms,
	uint32_t read_timeout_ms,
	otlp_socket_t *reuse_socket,
	otlp_http_request_t **out);

/* Same as _build_span_request, but for a batch of `metrics`. The
 * request targets `/v1/metrics`. */
otlp_status_t
otlp_exporter_otel_build_metric_request(const struct otlp_http_url *url,
	const char *user_agent,
	const char *service_name,
	const otlp_resource_attr_t *resource_attributes,
	size_t n_resource_attributes,
	const otlp_metric_t *const *metrics,
	size_t n_metrics,
	uint32_t connect_timeout_ms,
	uint32_t read_timeout_ms,
	otlp_socket_t *reuse_socket,
	otlp_http_request_t **out);

/* Same as _build_span_request, but for a batch of `logs`. The
 * request targets `/v1/logs`. */
otlp_status_t
otlp_exporter_otel_build_log_request(const struct otlp_http_url *url,
	const char *user_agent,
	const char *service_name,
	const otlp_resource_attr_t *resource_attributes,
	size_t n_resource_attributes,
	const otlp_log_record_t *const *logs,
	size_t n_logs,
	uint32_t connect_timeout_ms,
	uint32_t read_timeout_ms,
	otlp_socket_t *reuse_socket,
	otlp_http_request_t **out);

#endif
