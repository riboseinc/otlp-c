/* SPDX-License-Identifier: Apache-2.0 */
/*
 * SpanContext propagation. See include/otlp-c/context.h.
 *
 * Uses the traceparent format from w3c.c. The carrier abstraction
 * (callback-based setter/getter) keeps the library transport-
 * agnostic: the caller decides whether the carrier is an HTTP
 * header map, gRPC metadata, a message attribute table, or
 * something else entirely.
 */
#include <otlp-c/context.h>
#include <otlp-c/span.h>
#include <otlp-c/status.h>
#include <otlp-c/w3c.h>

#include "span_internal.h"

#include <stdint.h>
#include <string.h>

const char OTLP_CONTEXT_TRACEPARENT_HEADER[] = "traceparent";
const char OTLP_CONTEXT_TRACESTATE_HEADER[]  = "tracestate";

otlp_context_t
otlp_context_from_span(const otlp_span_t *span)
{
	otlp_context_t ctx = { 0 };
	const uint8_t *trace_id;
	const uint8_t *span_id;

	if (!span) {
		ctx.has_context = false;
		return ctx;
	}
	trace_id = otlp_span_get_trace_id(span);
	span_id  = otlp_span_get_span_id(span);
	if (!trace_id || !span_id) {
		ctx.has_context = false;
		return ctx;
	}
	memcpy(ctx.trace_id, trace_id, OTLP_TRACE_ID_LEN);
	memcpy(ctx.span_id,  span_id,  OTLP_SPAN_ID_LEN);
	ctx.sampled     = otlp_span_is_sampled(span);
	ctx.has_context = true;
	return ctx;
}

otlp_status_t
otlp_context_inject(otlp_context_t     ctx,
		    otlp_carrier_set_fn set,
		    void	      *carrier_ctx)
{
	char buf[OTLP_TRACEPARENT_BUF_SIZE];

	if (!set)
		return OTLP_ERR_NULL;
	if (!ctx.has_context)
		return OTLP_ERR_INVALID_ARGUMENT;

	/* Build the traceparent value manually using the format from
	 * w3c.c. We can't call otlp_traceparent_format() because it
	 * takes an otlp_span_t*, but ctx is a value type. The format
	 * is identical. */
	{
		static const char hex[] = "0123456789abcdef";
		size_t	       i;

		buf[0] = '0';
		buf[1] = '0';
		buf[2] = '-';
		for (i = 0; i < OTLP_TRACE_ID_LEN; i++) {
			buf[3 + i * 2]     = hex[(ctx.trace_id[i] >> 4) & 0x0F];
			buf[3 + i * 2 + 1] = hex[ctx.trace_id[i] & 0x0F];
		}
		buf[35] = '-';
		for (i = 0; i < OTLP_SPAN_ID_LEN; i++) {
			buf[36 + i * 2]     = hex[(ctx.span_id[i] >> 4) & 0x0F];
			buf[36 + i * 2 + 1] = hex[ctx.span_id[i] & 0x0F];
		}
		buf[52] = '-';
		buf[53] = '0';
		buf[54] = ctx.sampled ? '1' : '0';
		buf[55] = '\0';
	}

	return set(carrier_ctx, OTLP_CONTEXT_TRACEPARENT_HEADER, buf);
}

otlp_context_t
otlp_context_extract(otlp_carrier_get_fn get,
		     void	       *carrier_ctx)
{
	otlp_context_t ctx = { 0 };
	const char	    *header;
	uint8_t		    flags;

	if (!get) {
		ctx.has_context = false;
		return ctx;
	}
	header = get(carrier_ctx, OTLP_CONTEXT_TRACEPARENT_HEADER);
	if (!header) {
		ctx.has_context = false;
		return ctx;
	}

	/* Reuse the W3C parser. */
	if (otlp_traceparent_parse(header, ctx.trace_id, ctx.span_id, &flags) != OTLP_OK) {
		ctx.has_context = false;
		return ctx;
	}
	ctx.sampled     = (flags & 0x01) != 0;
	ctx.has_context = true;
	return ctx;
}
