/* SPDX-License-Identifier: Apache-2.0 */
#ifndef OTLP_C_LOG_INTERNAL_H
#define OTLP_C_LOG_INTERNAL_H

#include <otlp-c/log.h>
#include "span_internal.h"

#include <stddef.h>
#include <stdint.h>

#define OTLP_TRACE_ID_LEN 16
#define OTLP_SPAN_ID_LEN   8

struct otlp_log_record {
	otlp_severity_t	severity;
	char	       *severity_text;
	char	       *body;
	uint64_t	timestamp;
	bool		has_timestamp;
	uint8_t	trace_id[OTLP_TRACE_ID_LEN];
	uint8_t	span_id[OTLP_SPAN_ID_LEN];
	bool		has_trace;
	struct otlp_attribute attrs[128];
	size_t		n_attrs;
};

otlp_severity_t		otlp_log_get_severity(const otlp_log_record_t *lr);
const char	       *otlp_log_get_severity_text(const otlp_log_record_t *lr);
const char	       *otlp_log_get_body(const otlp_log_record_t *lr);
uint64_t		otlp_log_get_timestamp(const otlp_log_record_t *lr);
bool			otlp_log_has_timestamp(const otlp_log_record_t *lr);
const uint8_t	       *otlp_log_get_trace_id(const otlp_log_record_t *lr);
const uint8_t	       *otlp_log_get_span_id(const otlp_log_record_t *lr);
bool			otlp_log_has_trace(const otlp_log_record_t *lr);
const struct otlp_attribute *otlp_log_get_attrs(const otlp_log_record_t *lr, size_t *n);

/* Deep-copy a log record. Returns NULL on OOM. The caller owns
 * the result; free with otlp_log_record_free. */
otlp_log_record_t *otlp_log_record_clone(const otlp_log_record_t *src);

#endif
