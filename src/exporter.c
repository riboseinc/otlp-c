/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Exporter — STUB.
 *
 * Phase 5 of the roadmap. Real implementation will:
 *
 *   1. Hold a span ring buffer + background flush thread.
 *   2. Batching: when batch_size spans accumulate OR batch_ms
 *      elapses, trigger a flush.
 *   3. Flushing: encode the batch via exporter_otel.c, POST via
 *      http_client.c, handle retries.
 *   4. Shutdown: stop accepting new spans, drain the buffer,
 *      join the background thread.
 *
 * Thread-safety: the public API is thread-safe. The buffer
 * mutation, batch trigger, and retry counter all live behind the
 * same mutex.
 */
#include <otlp-c/exporter.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct otlp_exporter {
	char placeholder; /* TODO Phase 5 */
};

otlp_exporter_t *otlp_exporter_create(const otlp_exporter_opts_t *opts)
{
	(void)opts;
	return NULL;
}

void otlp_exporter_free(otlp_exporter_t *exp)
{
	(void)exp;
}

otlp_status_t otlp_exporter_emit(otlp_exporter_t *exp,
				 const otlp_span_t *span)
{
	(void)exp;
	(void)span;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_exporter_flush(otlp_exporter_t *exp)
{
	(void)exp;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_exporter_shutdown(otlp_exporter_t *exp)
{
	(void)exp;
	return OTLP_ERR_NOT_IMPLEMENTED;
}

otlp_status_t otlp_exporter_get_stats(otlp_exporter_t *exp,
				      otlp_exporter_stats_t *out)
{
	(void)exp;
	if (out)
		memset(out, 0, sizeof(*out));
	return OTLP_ERR_NOT_IMPLEMENTED;
}
