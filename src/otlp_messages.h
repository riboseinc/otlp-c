/* SPDX-License-Identifier: Apache-2.0 */
/*
 * OTLP message encoders — turn otlp_span_t* into protobuf wire bytes.
 *
 * Design (see plan D1): no parallel C-struct layer mirroring the
 * .proto. The encoder walks the public span accessors and emits
 * wire bytes directly. Field numbers from docs/otlp-spec.md live
 * as #defines grouped by message.
 *
 * Sub-message pattern: each emit_X function takes a parent buf and
 * a field number, allocates a temp buf for the sub-message body,
 * encodes into it, wraps it via otlp_pb_field_message, and frees
 * the temp buf. The pattern is identical at each level; the only
 * variation is what fields the body emits.
 *
 * v0.1.0 scope: string/int64/double/bool/bytes attributes; no
 * events, links, trace_state, dropped_*_count, schema_url, or
 * flags. All are easily added later by extending the per-message
 * encoders without breaking callers (OCP).
 */
#ifndef OTLP_C_OTLP_MESSAGES_H
#define OTLP_C_OTLP_MESSAGES_H

#include "protobuf_encode.h"
#include "span_internal.h"

#include <stddef.h>

/* Encode a complete ExportTraceServiceRequest body into `out`.
 *
 * `service_name` becomes the value of the resource attribute
 * "service.name" (skipped if NULL or empty).
 * `scope_name` / `scope_version` populate InstrumentationScope
 * (skipped if both NULL or empty).
 * `spans` is an array of `n_spans` span pointers.
 *
 * Returns OTLP_OK on success, an OTLP_ERR_* on encoding or memory
 * failure. On failure, `out` may contain a partial body — caller
 * must reset or free it.
 *
 * Empty request (no service name, no scope, no spans) produces a
 * zero-length body, matching the spec. */
otlp_status_t
otlp_encode_export_trace_service_request(struct otlp_pb_buf *out,
	const char *service_name,
	const char *scope_name,
	const char *scope_version,
	const otlp_span_t *const *spans,
	size_t n_spans);

/* Encode a single KeyValue into `out` (key + AnyValue oneof).
 * Exposed for tests; production callers use the top-level encoder. */
otlp_status_t
otlp_encode_key_value(struct otlp_pb_buf *out,
	const char *key,
	const struct otlp_attribute *attr);

/* Encode a single Span body into `out` (no ResourceSpans wrapping). */
otlp_status_t
otlp_encode_span_body(struct otlp_pb_buf *out, const otlp_span_t *span);

#endif
