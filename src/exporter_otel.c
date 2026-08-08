/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Exporter-internal: build an HTTP POST from a batch of spans.
 * See src/exporter_otel.h.
 */
#include "exporter_otel.h"
#include "http_client.h"
#include "otlp_messages.h"
#include "protobuf_encode.h"

#include <otlp-c/exporter.h>
#include <otlp-c/span.h>

#include <stddef.h>
#include <stdint.h>

otlp_status_t
otlp_exporter_otel_build_request(const struct otlp_http_url *url,
	const char *user_agent,
	const char *service_name,
	const otlp_resource_attr_t *resource_attributes,
	size_t n_resource_attributes,
	const otlp_span_t *const *spans,
	size_t n_spans,
	otlp_socket_t *reuse_socket,
	otlp_http_request_t **out)
{
	struct otlp_pb_buf body = { 0 };
	otlp_status_t st;

	if (!url || !out)
		return OTLP_ERR_NULL;

	st = otlp_pb_buf_init(&body, 0);
	if (st != OTLP_OK)
		return st;
	st = otlp_encode_export_trace_service_request(
		&body, service_name,
		resource_attributes, n_resource_attributes,
		NULL, NULL, spans, n_spans);
	if (st != OTLP_OK)
	{
		otlp_pb_buf_free(&body);
		return st;
	}

	if (reuse_socket)
		st = otlp_http_request_start_with_socket(
			out, url, user_agent, body.data, body.len, reuse_socket);
	else
		st = otlp_http_request_start(
			out, url, user_agent, body.data, body.len);
	otlp_pb_buf_free(&body);
	return st;
}
