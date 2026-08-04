/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Span lifecycle and attribute storage — STUB.
 *
 * Phase 4 of the roadmap (see docs/roadmap.md). Replace this file
 * with a real implementation that:
 *
 *   1. Allocates a struct otlp_span with a name, kind, status,
 *      attributes, and timestamps.
 *   2. Manages a small dynamic array of attributes ( KeyValue ).
 *   3. Generates random trace_id and span_id on construction
 *      (when called via the tracer).
 *
 * Property tests for this module live in tests/property/.
 */
#include <otlp-c/span.h>

#include <stddef.h>
#include <stdlib.h>

struct otlp_span {
	char placeholder; /* TODO Phase 4 */
};

otlp_span_t *otlp_span_create(const char *name)
{
	(void)name;
	return NULL;
}

void otlp_span_free(otlp_span_t *span)
{
	(void)span;
}

otlp_status_t otlp_span_set_trace_id(otlp_span_t *span,
				     const uint8_t *trace_id)
{
	(void)span;
	(void)trace_id;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_set_span_id(otlp_span_t *span,
				    const uint8_t *span_id)
{
	(void)span;
	(void)span_id;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_set_parent_span_id(otlp_span_t *span,
					   const uint8_t *parent)
{
	(void)span;
	(void)parent;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_set_start_time(otlp_span_t *span,
				       uint64_t unix_nano)
{
	(void)span;
	(void)unix_nano;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_set_end_time(otlp_span_t *span,
				     uint64_t unix_nano)
{
	(void)span;
	(void)unix_nano;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_mark_start(otlp_span_t *span)
{
	(void)span;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_mark_end(otlp_span_t *span)
{
	(void)span;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_set_kind(otlp_span_t *span,
				 otlp_span_kind_t kind)
{
	(void)span;
	(void)kind;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_set_name(otlp_span_t *span, const char *name)
{
	(void)span;
	(void)name;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_set_attribute_string(otlp_span_t *span,
					     const char *key,
					     const char *value)
{
	(void)span;
	(void)key;
	(void)value;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_set_attribute_int(otlp_span_t *span,
					  const char *key,
					  int64_t value)
{
	(void)span;
	(void)key;
	(void)value;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_set_attribute_double(otlp_span_t *span,
					     const char *key,
					     double value)
{
	(void)span;
	(void)key;
	(void)value;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_set_attribute_bool(otlp_span_t *span,
					   const char *key,
					   bool value)
{
	(void)span;
	(void)key;
	(void)value;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_set_attribute_bytes(otlp_span_t *span,
					    const char *key,
					    const uint8_t *bytes,
					    size_t len)
{
	(void)span;
	(void)key;
	(void)bytes;
	(void)len;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_span_set_status(otlp_span_t *span,
				   otlp_status_code_t code,
				   const char *description)
{
	(void)span;
	(void)code;
	(void)description;
	return OTLP_ERR_NOT_IMPLEMENTED;
}
