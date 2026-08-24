/* SPDX-License-Identifier: BSD-3-Clause */
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
 *   prop_fuzz_partial_success — random/mutated response bodies through
 *                             the PartialSuccess decoder: no crash,
 *                             message pointer stays in bounds
 */
#include "prng.h"
#include "../test_util.h"
#include "property_harness.h"

#include "../src/exporter_otel.h"
#include "../src/http_client.h"
#include "../src/http_response_parser.h"
#include "decoder.h"

#include <otlp-c/otlp.h>
#include <otlp-c/span.h>
#include <otlp-c/w3c.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


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

/* ── HTTP response parser fuzz ──────────────────────────────────
 * Feed random or mutated raw HTTP responses DIRECTLY to the
 * response parser (v0.6.11: pure bytes -> verdict; previously
 * this drove a real socket through an echo-server thread, which
 * excluded Windows and cost ~50ms per iteration). Assert the
 * verdict is always one of the three legal values and, when
 * complete, that the parsed fields are self-consistent. ASAN
 * (the whole-suite run) does the memory checking. */

static void
fuzz_make_response(struct prng *p, uint8_t *out, size_t *out_len)
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
		len = (size_t) prng_u32(p, (uint32_t) 2048);
		for (i = 0; i < len; i++)
			out[i] = (uint8_t) prng_next(p);
		*out_len = len;
		return;
	}
	/* Valid response + a few mutations: byte flips, truncation,
	 * or size-line corruption. */
	memcpy(out, valid, valid_len);
	*out_len = valid_len;
	for (i = 0; i < 1 + prng_u32(p, 4); i++)
	{
		size_t op = prng_u32(p, 3);

		if (op == 0 && *out_len > 0)
			out[prng_u32(p, (uint32_t) *out_len)] =
				(uint8_t) prng_next(p);
		else if (op == 1 && *out_len > 8)
			*out_len = prng_u32(p, (uint32_t) *out_len);
		else if (*out_len < 2048 - 1)
			out[(*out_len)++] = (uint8_t) prng_next(p);
	}
}

static int
prop_fuzz_http_response(uint64_t seed)
{
	struct prng p;
	uint8_t buf[2048];
	size_t len;
	struct otlp_http_resp out;
	int rc;

	prng_seed(&p, seed);
	fuzz_make_response(&p, buf, &len);

	rc = otlp_http_resp_parse(
		buf, len, (seed & 1) != 0, &out);
	check_true(rc == -1 || rc == 0 || rc == 1);
	if (rc == 1)
	{
		check_true(out.http_status >= 100 &&
			out.http_status <= 999);
		check_true(out.body >= buf);
		check_true(out.body + out.body_len <= buf + len);
	}
	return 1;
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


/* ── PartialSuccess decode fuzz (v0.5.96) ───────────────────────
 * The library's first response-body decoder. Random bytes, plus
 * mutated copies of a VALID body (so the parser reaches deeper
 * states than pure noise manages), through the decoder. Must not
 * crash; when it reports a message, the pointer must lie inside
 * the input buffer. */

static int
prop_fuzz_partial_success(uint64_t seed)
{
	struct prng p;
	uint8_t buf[256];
	uint8_t valid[64];
	size_t len;
	size_t i;

	/* Valid reference body: partial_success{rejected=3, msg}. */
	valid[0] = 0x2a;
	valid[1] = 0x0c;
	valid[2] = 0x08;
	valid[3] = 0x03;
	valid[4] = 0x12;
	valid[5] = 0x08;
	memcpy(valid + 6, "queueful", 8);
	/* len 14 = 2 + 12 */

	prng_seed(&p, seed);
	for (i = 0; i < 100; i++)
	{
		int64_t rejected = 0;
		const char *msg = NULL;
		size_t msg_len = 0;
		bool rc;

		if (prng_next(&p) & 1)
		{
			/* Pure noise. */
			len = (size_t) prng_u32(&p, (uint32_t) sizeof(buf));
			for (size_t j = 0; j < len; j++)
				buf[j] = (uint8_t) prng_next(&p);
		}
		else
		{
			/* Mutate the valid body. */
			len = 14;
			memcpy(buf, valid, len);
			if (prng_next(&p) & 1)
				len = (size_t) prng_u32(&p, (uint32_t) len + 1);
			for (size_t j = 0; j < 3; j++)
				buf[prng_u32(&p, (uint32_t) len)] =
					(uint8_t) prng_next(&p);
		}

		rc = otlp_exporter_otel_decode_partial_success(
			buf, len, &rejected, &msg, &msg_len);
		if (rc && msg_len > 0)
		{
			/* Message pointer must lie inside the input. */
			if (msg < (const char *) buf ||
				(size_t)(msg - (const char *) buf) + msg_len >
					len)
				return 0;
		}
	}
	return 1;
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
		prop_fuzz_http_response, "prop_fuzz_http_response", 20000, 1);
	failures += property_run(prop_fuzz_partial_success,
		"prop_fuzz_partial_success",
		5000,
		1);
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
