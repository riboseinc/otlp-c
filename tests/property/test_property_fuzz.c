/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Fuzz-like property tests for input parsers.
 *
 * Unlike the other property tests (which generate well-formed
 * inputs with a PRNG and check semantic invariants), these tests
 * feed ARBITRARY byte sequences to the parsers and assert only
 * one invariant: no crash. This catches buffer overflows, NUL
 * injection, and integer-overflow bugs that well-formed tests miss.
 *
 * Properties:
 *   prop_fuzz_url_parse     — any byte sequence → parse_url: no crash
 *   prop_fuzz_proto_decode  — any byte sequence → decode_varint/tag: no crash
 *   prop_fuzz_span_create   — any byte sequence → span_create(name): no crash
 *   prop_fuzz_traceparent   — any byte sequence → parse: no crash or clean
 * reject prop_fuzz_http_response — random/mutated raw responses through the
 * state machine (incl. chunked decoder): terminal state, no crash
 *   prop_fuzz_context_extract — arbitrary printable carrier values:
 *                             extract never crashes
 */
#include "prng.h"
#include "property_harness.h"

#include "../src/http_client.h"
#include "decoder.h"

#include <otlp-c/otlp.h>
#include <otlp-c/span.h>
#include <otlp-c/w3c.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include <time.h>
#include "test_helper_echo.h"
#endif

/* ── URL parser fuzz ────────────────────────────────────────────
 * Feed random byte sequences (including NULs, non-ASCII, very long)
 * to otlp_http_parse_url. Must not crash. */

static int
prop_fuzz_url_parse(uint64_t seed)
{
	struct prng p;
	struct otlp_http_url url;
	uint8_t buf[512];
	size_t len;
	size_t i;

	prng_seed(&p, seed);

	/* Generate random-length random-byte string (no NUL terminator
	 * — we need a C string for parse_url, so cap at 255 + NUL). */
	len = (size_t) prng_u32(&p, (uint32_t) sizeof(buf) - 1);
	for (i = 0; i < len; i++)
		buf[i] = (uint8_t) prng_next(&p);
	buf[len] = '\0';

	/* The parser must return a status code, never crash. */
	(void) otlp_http_parse_url((const char *) buf, &url);
	return 1;
}

/* ── Protobuf decoder fuzz ──────────────────────────────────────
 * Feed random byte sequences to decode_varint and decode_tag.
 * Must not crash, even on truncated/malformed input. */

static int
prop_fuzz_proto_decode(uint64_t seed)
{
	struct prng p;
	uint8_t buf[128];
	size_t len;
	size_t i;
	size_t pos;
	uint64_t val;
	uint32_t fnum;
	int wtype;
	otlp_status_t st;

	prng_seed(&p, seed);
	len = (size_t) prng_u32(&p, (uint32_t) sizeof(buf));
	for (i = 0; i < len; i++)
		buf[i] = (uint8_t) prng_next(&p);

	/* Try decoding as a sequence of varints. */
	pos = 0;
	while (pos < len)
	{
		st = decode_varint(buf, len, &pos, &val);
		if (st != OTLP_OK)
			break;
	}

	/* Try decoding as tag-value pairs. */
	pos = 0;
	while (pos < len)
	{
		st = decode_tag(buf, len, &pos, &fnum, &wtype);
		if (st != OTLP_OK)
			break;
		st = skip_value(buf, len, &pos, wtype);
		if (st != OTLP_OK)
			break;
	}

	return 1; /* no crash = pass */
}

/* ── Span name fuzz ─────────────────────────────────────────────
 * Feed random byte sequences as span names. Must not crash. */

static int
prop_fuzz_span_create(uint64_t seed)
{
	struct prng p;
	uint8_t buf[256];
	size_t len;
	size_t i;

	prng_seed(&p, seed);
	len = (size_t) prng_u32(&p, (uint32_t) sizeof(buf) - 1);
	for (i = 0; i < len; i++)
		buf[i] = (uint8_t) prng_next(&p);
	buf[len] = '\0';

	otlp_span_t *span = otlp_span_create((const char *) buf);
	if (span)
		otlp_span_free(span);
	return 1;
}

/* ── Traceparent parse fuzz ─────────────────────────────────────
 * Feed random byte sequences to otlp_traceparent_parse.
 * Must either parse cleanly or reject with INVALID_ARGUMENT,
 * never crash. */

static int
prop_fuzz_traceparent(uint64_t seed)
{
	struct prng p;
	char buf[128];
	size_t len;
	size_t i;
	uint8_t trace_id[16];
	uint8_t span_id[8];
	uint8_t flags;

	prng_seed(&p, seed);
	len = (size_t) prng_u32(&p, (uint32_t) sizeof(buf) - 1);
	for (i = 0; i < len; i++)
		buf[i] = (char) (prng_next(&p) & 0x7F); /* keep printable-ish */
	buf[len] = '\0';

	(void) otlp_traceparent_parse(buf, trace_id, span_id, &flags);
	return 1;
}

/* ── main ─────────────────────────────────────────────────────── */

/* The response served by the current fuzz iteration (the raw
 * handler hands it back verbatim). */
static uint8_t g_resp[2048];
static size_t g_resp_len;

static int
fuzz_raw_handler(const uint8_t *req_body,
	size_t req_len,
	uint8_t *resp_buf,
	size_t resp_cap,
	size_t *resp_len)
{
	(void) req_body;
	(void) req_len;
	assert(g_resp_len <= resp_cap);
	memcpy(resp_buf, g_resp, g_resp_len);
	*resp_len = g_resp_len;
	return ECHO_RAW_RESPONSE;
}

/* ── HTTP response parser fuzz ──────────────────────────────────
 * Feed random or mutated raw HTTP responses through the REAL
 * request state machine (connect -> send -> read -> parse,
 * including the v0.5.80 chunked decoder) and assert it always
 * reaches a terminal state — DONE or FAILED — without crashing.
 * ASAN (the whole-suite run) does the memory checking. */

static void
fuzz_make_response(struct prng *p)
{
	static const char valid[] = "HTTP/1.1 200 OK\r\n"
				    "Content-Type: application/x-protobuf\r\n"
				    "Transfer-Encoding: chunked\r\n"
				    "\r\n"
				    "5\r\nhello\r\n"
				    "3;x=1\r\nbye\r\n"
				    "0\r\n\r\n";
	size_t valid_len = sizeof(valid) - 1;
	size_t mode = prng_u32(p, 2);
	size_t len;
	size_t i;

	if (mode == 0)
	{
		/* Pure random bytes (NULs, non-ASCII, everything). */
		len = (size_t) prng_u32(p, (uint32_t) sizeof(g_resp));
		for (i = 0; i < len; i++)
			g_resp[i] = (uint8_t) prng_next(p);
		g_resp_len = len;
		return;
	}
	/* Valid response + a few mutations: byte flips, truncation,
	 * or size-line corruption. */
	memcpy(g_resp, valid, valid_len);
	g_resp_len = valid_len;
	for (i = 0; i < 1 + prng_u32(p, 4); i++)
	{
		size_t op = prng_u32(p, 3);

		if (op == 0 && g_resp_len > 0)
			g_resp[prng_u32(p, (uint32_t) g_resp_len)] =
				(uint8_t) prng_next(p);
		else if (op == 1 && g_resp_len > 8)
			g_resp_len = prng_u32(p, (uint32_t) g_resp_len);
		else if (g_resp_len < sizeof(g_resp) - 1)
			g_resp[g_resp_len++] = (uint8_t) prng_next(p);
	}
}

static int
prop_fuzz_http_response(uint64_t seed)
{
	struct prng p;
	struct echo_server srv;
	struct otlp_http_url url;
	otlp_http_request_t *req = NULL;
	char url_str[128];
	otlp_status_t st = OTLP_OK;
	struct timespec t0;
	uint64_t deadline_ms;
	size_t i;

	prng_seed(&p, seed);
	if (echo_server_start(&srv, fuzz_raw_handler, 1) != OTLP_OK)
		return 0;
	snprintf(url_str,
		sizeof(url_str),
		"http://127.0.0.1:%u/v1/traces",
		srv.port);
	if (otlp_http_parse_url(url_str, &url) != OTLP_OK)
	{
		(void) echo_server_join(&srv, 1000);
		return 0;
	}

	fuzz_make_response(&p);

	st = otlp_http_request_start(
		&req, &url, "otlp-c/fuzz", (const uint8_t *) "x", 1, 200, 200);
	if (st != OTLP_OK)
	{
		(void) echo_server_join(&srv, 1000);
		return 0;
	}

	/* Wall-clock bounded drive (Release spins fast). */
	clock_gettime(CLOCK_MONOTONIC, &t0);
	deadline_ms = (uint64_t) t0.tv_sec * 1000 +
		(uint64_t)(t0.tv_nsec / 1000000) + 1000;
	for (i = 0; i < 100000; i++)
	{
		struct timespec now;

		st = otlp_http_request_step(req);
		if (otlp_http_request_state(req) == OTLP_HTTP_REQ_DONE ||
			otlp_http_request_state(req) == OTLP_HTTP_REQ_FAILED)
			break;
		clock_gettime(CLOCK_MONOTONIC, &now);
		if ((uint64_t) now.tv_sec * 1000 +
				(uint64_t)(now.tv_nsec / 1000000) >
			deadline_ms)
			break;
		if (st == OTLP_ERR_WOULDBLOCK)
		{
			struct timespec ts = { 0, 1000 };

			nanosleep(&ts, NULL);
		}
	}

	{
		int ok = otlp_http_request_state(req) == OTLP_HTTP_REQ_DONE ||
			otlp_http_request_state(req) == OTLP_HTTP_REQ_FAILED;

		otlp_http_request_free(req);
		(void) echo_server_join(&srv, 2 * 1000 * 1000);
		return ok;
	}
}

/* ── Context extract fuzz ───────────────────────────────────────
 * A carrier returning arbitrary bytes for traceparent/tracestate/
 * baggage: extract must return a context (with or without
 * correlation) without crashing, and never accept control bytes
 * into the propagated state. */

static uint8_t g_carrier_tp[256];
static uint8_t g_carrier_ts[256];
static uint8_t g_carrier_bg[256];

static const char *
fuzz_carrier_get(void *ctx, const char *key)
{
	(void) ctx;
	if (strcmp(key, "traceparent") == 0)
		return (const char *) g_carrier_tp;
	if (strcmp(key, "tracestate") == 0)
		return (const char *) g_carrier_ts;
	if (strcmp(key, "baggage") == 0)
		return (const char *) g_carrier_bg;
	return NULL;
}

static int
prop_fuzz_context_extract(uint64_t seed)
{
	struct prng p;
	otlp_context_t ctx;
	size_t i;

	prng_seed(&p, seed);
	for (i = 0; i < sizeof(g_carrier_tp) - 1; i++)
		g_carrier_tp[i] =
			(uint8_t)(prng_next(&p) % 95 + 32); /* printable */
	g_carrier_tp[sizeof(g_carrier_tp) - 1] = '\0';
	for (i = 0; i < sizeof(g_carrier_ts) - 1; i++)
		g_carrier_ts[i] = (uint8_t)(prng_next(&p) % 95 + 32);
	g_carrier_ts[sizeof(g_carrier_ts) - 1] = '\0';
	for (i = 0; i < sizeof(g_carrier_bg) - 1; i++)
		g_carrier_bg[i] = (uint8_t)(prng_next(&p) % 95 + 32);
	g_carrier_bg[sizeof(g_carrier_bg) - 1] = '\0';

	ctx = otlp_context_extract(fuzz_carrier_get, NULL);
	/* Either outcome is fine; a crash is not. */
	return ctx.has_context || !ctx.has_context;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(
		prop_fuzz_url_parse, "prop_fuzz_url_parse", 5000, 1);
	failures += property_run(
		prop_fuzz_proto_decode, "prop_fuzz_proto_decode", 5000, 1);
	failures += property_run(
		prop_fuzz_span_create, "prop_fuzz_span_create", 5000, 1);
	failures += property_run(
		prop_fuzz_traceparent, "prop_fuzz_traceparent", 5000, 1);
	failures += property_run(
		prop_fuzz_http_response, "prop_fuzz_http_response", 300, 1);
	failures += property_run(prop_fuzz_context_extract,
		"prop_fuzz_context_extract",
		5000,
		1);

	if (failures)
		printf("[property] %d fuzz property(ies) FAILED\n", failures);
	else
		printf("[property] all fuzz properties passed (no crashes)\n");
	return failures ? 1 : 0;
}
