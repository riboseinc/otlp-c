/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Synchronous flush delivery (v1.1.2): the one-shot encode →
 * POST → retry → event pipeline behind otlp_exporter_flush_metric
 * and otlp_exporter_flush_log. Extracted from exporter.c — the
 * async tick pipeline and this module are the two delivery
 * engines; they share struct otlp_exporter (exporter_internal.h)
 * and retry_policy, nothing else.
 */
#include "exporter_internal.h"

#include "http_client.h"
#include "retry_policy.h"

#include <otlp-c/log.h>
#include <otlp-c/metric.h>

#include <otlp-c/exporter.h>

#include "otlp_messages.h"
#include "otlp_schema.h"
#include "protobuf_encode.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#if !defined(_WIN32)
#include <time.h>
#endif

/* ── Synchronous metric / log flush ───────────────────────────── */

/* One POST attempt. Returns OTLP_OK on 2xx; sets *got_response
 * when the server answered at all (any HTTP status). A network-
 * level failure before a response (*got_response == false) is
 * transient — the caller retries with the async path's backoff
 * budget. A non-2xx response is permanent for the sync path. */
static otlp_status_t
flush_post_once(struct otlp_exporter *e,
	otlp_signal_id_t signal,
	const struct otlp_http_url *url,
	const uint8_t *body,
	size_t body_len,
	bool *got_response)
{
	otlp_http_request_t *req = NULL;
	otlp_status_t st;
	uint64_t deadline;
	uint64_t now;
	st = otlp_http_request_start(&req,
		url,
		e->user_agent,
		e->http_headers,
		e->n_http_headers,
		body,
		body_len,
		e->connect_timeout_ms,
		e->read_timeout_ms);
	if (st != OTLP_OK)
	{
		otlp_event_t ev = {
			.code = OTLP_EVT_SYNC_FLUSH_FAILED,
			.level = OTLP_LOG_ERROR,
			.signal = signal,
			.status = st,
		};

		otlp_exporter_event_log(e, &ev);
		return st;
	}
	deadline = otlp_platform_now_mono_ms() + e->flush_timeout_ms;
	for (;;)
	{
		st = otlp_http_request_step(req);
		otlp_http_req_state_t s = otlp_http_request_state(req);

		if (s == OTLP_HTTP_REQ_DONE)
		{
			int http = otlp_http_request_http_status(req);

			if (http >= 200 && http < 300)
			{
				/* Surface server-reported data loss, if any
				 * (no stats counter on the one-shot path —
				 * the message is the observability surface).
				 * path + 5 skips "/v1/" to the signal name. */
				size_t blen = 0;
				const uint8_t *b =
					otlp_http_request_body(req, &blen);

				if (b && blen > 0)
					otlp_exporter_report_partial_success(
						e, signal, NULL, 0, b, blen);
			}
			otlp_http_request_free(req);
			*got_response = true;
			if (http >= 200 && http < 300)
				return OTLP_OK;
			otlp_event_t ev = {
				.code = OTLP_EVT_SYNC_FLUSH_FAILED,
				.level = OTLP_LOG_ERROR,
				.signal = signal,
				.http_status = http,
			};

			otlp_exporter_event_log(e, &ev);
			return OTLP_ERR_NETWORK;
		}
		if (s == OTLP_HTTP_REQ_FAILED)
		{
			otlp_event_t ev = {
				.code = OTLP_EVT_SYNC_FLUSH_FAILED,
				.level = OTLP_LOG_ERROR,
				.signal = signal,
				.status = OTLP_ERR_NETWORK,
			};

			otlp_http_request_free(req);
			otlp_exporter_event_log(e, &ev);
			return OTLP_ERR_NETWORK;
		}
		if (st != OTLP_OK && st != OTLP_ERR_WOULDBLOCK)
		{
			otlp_event_t ev = {
				.code = OTLP_EVT_SYNC_FLUSH_FAILED,
				.level = OTLP_LOG_ERROR,
				.signal = signal,
				.status = st,
			};

			otlp_http_request_free(req);
			otlp_exporter_event_log(e, &ev);
			return st;
		}
		now = otlp_platform_now_mono_ms();
		if (now >= deadline)
		{
			otlp_event_t ev = {
				.code = OTLP_EVT_SYNC_FLUSH_FAILED,
				.level = OTLP_LOG_ERROR,
				.signal = signal,
				.status = OTLP_ERR_TIMEOUT,
				.timeout_ms = e->flush_timeout_ms,
			};

			otlp_http_request_free(req);
			otlp_exporter_event_log(e, &ev);
			return OTLP_ERR_TIMEOUT;
		}
#if defined(_WIN32)
		Sleep(1);
#else
		{
			struct timespec ts = { 0, 1000 * 1000 };
			nanosleep(&ts, NULL);
		}
#endif
	}
	otlp_http_request_free(req);
	return OTLP_ERR_TIMEOUT;
}

static otlp_status_t
flush_sync(struct otlp_exporter *e,
	otlp_signal_id_t signal,
	const uint8_t *body,
	size_t body_len)
{
	otlp_status_t st = OTLP_ERR_NETWORK;
	bool got_response = false;
	uint32_t attempt;
	struct otlp_http_url *url;

	if (!e || (!body && body_len > 0))
		return OTLP_ERR_NULL;
	url = &e->sig[signal].url;
	if (e->null_transport)
		return OTLP_OK;
	/* Retry transient (pre-response) network failures with the
	 * same budget the async pipeline uses. The first connect in
	 * a fresh process occasionally fails transiently (DNS/order-
	 * of-addresses, collector still warming its accept queue);
	 * the async path recovers via backoff — the sync path
	 * deserves the same resilience. Non-2xx responses and
	 * timeouts are permanent (no retry). */
	for (attempt = 0; attempt <= e->max_retries; attempt++)
	{
		st = flush_post_once(
			e, signal, url, body, body_len, &got_response);
		if (st == OTLP_OK || got_response)
			return st;
		if (attempt < e->max_retries)
		{
			/* v1.0.5: one retry engine. The sync path draws its
			 * delay through the same jittered policy as the
			 * async path, with the sync-latency cap expressed
			 * as the config's max (was: a hand-rolled fixed
			 * backoff_initial_ms clamp). */
			struct otlp_retry_cfg cfg = {
				e->backoff_initial_ms,
				e->backoff_max_ms < 100 ? e->backoff_max_ms
							: 100,
			};
			uint32_t delay = otlp_retry_delay_ms(
				&e->jitter_prng, attempt + 1, 0, &cfg, NULL);
			otlp_event_t ev = {
				.code = OTLP_EVT_RETRY_ARMED,
				.level = OTLP_LOG_WARN,
				.signal = signal,
				.attempt = attempt + 1,
				.max_retries = e->max_retries,
				.delay_ms = delay,
			};

			otlp_exporter_event_log(e, &ev);
#if defined(_WIN32)
			Sleep(delay);
#else
			{
				struct timespec ts = { 0,
					(long) delay * 1000 * 1000 };
				nanosleep(&ts, NULL);
			}
#endif
		}
	}
	return st;
}

otlp_status_t
otlp_exporter_flush_metric(otlp_exporter_t *e, const otlp_metric_t *m)
{
	struct otlp_pb_buf body = { 0 };
	otlp_status_t st;
	const otlp_metric_t *arr[1];

	if (!e || !m)
		return OTLP_ERR_NULL;
	otlp_atomic_fetch_add_u64(
		&e->sig[SIGNAL_METRIC].emitted, 1, OTLP_MEMORY_ORDER_RELAXED);
	st = otlp_pb_buf_init(&body, 0);
	if (st != OTLP_OK)
	{
		/* Accounting invariant: emitted == sent + dropped_err.
		 * Pre-v0.5.59 this path returned without updating
		 * dropped_err, breaking the invariant under OOM. */
		otlp_atomic_fetch_add_u64(&e->sig[SIGNAL_METRIC].dropped_err,
			1,
			OTLP_MEMORY_ORDER_RELAXED);
		return st;
	}
	arr[0] = m;
	st = otlp_encode_export_metrics_service_request(&body,
		e->service_name,
		e->schema_url,
		e->resource_attrs.items,
		e->resource_attrs.n,
		NULL,
		NULL,
		arr,
		1);
	if (st == OTLP_OK)
		st = flush_sync(e, OTLP_SIGNAL_METRICS, body.data, body.len);
	otlp_pb_buf_free(&body);
	if (st == OTLP_OK)
		otlp_atomic_fetch_add_u64(&e->sig[SIGNAL_METRIC].sent,
			1,
			OTLP_MEMORY_ORDER_RELAXED);
	else
		otlp_atomic_fetch_add_u64(&e->sig[SIGNAL_METRIC].dropped_err,
			1,
			OTLP_MEMORY_ORDER_RELAXED);
	return st;
}

otlp_status_t
otlp_exporter_flush_log(otlp_exporter_t *e, const otlp_log_record_t *lr)
{
	struct otlp_pb_buf body = { 0 };
	otlp_status_t st;
	const otlp_log_record_t *arr[1];

	if (!e || !lr)
		return OTLP_ERR_NULL;
	otlp_atomic_fetch_add_u64(
		&e->sig[SIGNAL_LOG].emitted, 1, OTLP_MEMORY_ORDER_RELAXED);
	st = otlp_pb_buf_init(&body, 0);
	if (st != OTLP_OK)
	{
		/* Accounting invariant: emitted == sent + dropped_err.
		 * Pre-v0.5.59 this path returned without updating
		 * dropped_err, breaking the invariant under OOM. */
		otlp_atomic_fetch_add_u64(&e->sig[SIGNAL_LOG].dropped_err,
			1,
			OTLP_MEMORY_ORDER_RELAXED);
		return st;
	}
	arr[0] = lr;
	st = otlp_encode_export_logs_service_request(&body,
		e->service_name,
		e->schema_url,
		e->resource_attrs.items,
		e->resource_attrs.n,
		NULL,
		NULL,
		arr,
		1);
	if (st == OTLP_OK)
		st = flush_sync(e, OTLP_SIGNAL_LOGS, body.data, body.len);
	otlp_pb_buf_free(&body);
	if (st == OTLP_OK)
		otlp_atomic_fetch_add_u64(
			&e->sig[SIGNAL_LOG].sent, 1, OTLP_MEMORY_ORDER_RELAXED);
	else
		otlp_atomic_fetch_add_u64(&e->sig[SIGNAL_LOG].dropped_err,
			1,
			OTLP_MEMORY_ORDER_RELAXED);
	return st;
}

otlp_status_t
otlp_exporter_shutdown(otlp_exporter_t *e)
{
	if (!e)
		return OTLP_ERR_NULL;
	otlp_atomic_store_int(
		&e->shutdown_requested, 1, OTLP_MEMORY_ORDER_RELEASE);
	return OTLP_OK;
}

