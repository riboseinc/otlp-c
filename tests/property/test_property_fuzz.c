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
 *   prop_fuzz_traceparent   — any byte sequence → parse: no crash or clean reject
 */
#include "prng.h"
#include "property_harness.h"

#include "../src/http_client.h"
#include "decoder.h"

#include <otlp-c/otlp.h>
#include <otlp-c/span.h>
#include <otlp-c/w3c.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ── URL parser fuzz ────────────────────────────────────────────
 * Feed random byte sequences (including NULs, non-ASCII, very long)
 * to otlp_http_parse_url. Must not crash. */

static int
prop_fuzz_url_parse(uint64_t seed)
{
	struct prng		 p;
	struct otlp_http_url url;
	uint8_t			 buf[512];
	size_t			 len;
	size_t			 i;

	prng_seed(&p, seed);

	/* Generate random-length random-byte string (no NUL terminator
	 * — we need a C string for parse_url, so cap at 255 + NUL). */
	len = (size_t)prng_u32(&p, (uint32_t)sizeof(buf) - 1);
	for (i = 0; i < len; i++)
		buf[i] = (uint8_t)prng_next(&p);
	buf[len] = '\0';

	/* The parser must return a status code, never crash. */
	(void)otlp_http_parse_url((const char *)buf, &url);
	return 1;
}

/* ── Protobuf decoder fuzz ──────────────────────────────────────
 * Feed random byte sequences to decode_varint and decode_tag.
 * Must not crash, even on truncated/malformed input. */

static int
prop_fuzz_proto_decode(uint64_t seed)
{
	struct prng    p;
	uint8_t	    buf[128];
	size_t	    len;
	size_t	    i;
	size_t	    pos;
	uint64_t	    val;
	uint32_t	    fnum;
	int		    wtype;
	otlp_status_t    st;

	prng_seed(&p, seed);
	len = (size_t)prng_u32(&p, (uint32_t)sizeof(buf));
	for (i = 0; i < len; i++)
		buf[i] = (uint8_t)prng_next(&p);

	/* Try decoding as a sequence of varints. */
	pos = 0;
	while (pos < len) {
		st = decode_varint(buf, len, &pos, &val);
		if (st != OTLP_OK)
			break;
	}

	/* Try decoding as tag-value pairs. */
	pos = 0;
	while (pos < len) {
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
	uint8_t     buf[256];
	size_t      len;
	size_t      i;

	prng_seed(&p, seed);
	len = (size_t)prng_u32(&p, (uint32_t)sizeof(buf) - 1);
	for (i = 0; i < len; i++)
		buf[i] = (uint8_t)prng_next(&p);
	buf[len] = '\0';

	otlp_span_t *span = otlp_span_create((const char *)buf);
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
	char	    buf[128];
	size_t      len;
	size_t      i;
	uint8_t     trace_id[16];
	uint8_t     span_id[8];
	uint8_t     flags;

	prng_seed(&p, seed);
	len = (size_t)prng_u32(&p, (uint32_t)sizeof(buf) - 1);
	for (i = 0; i < len; i++)
		buf[i] = (char)(prng_next(&p) & 0x7F); /* keep printable-ish */
	buf[len] = '\0';

	(void)otlp_traceparent_parse(buf, trace_id, span_id, &flags);
	return 1;
}

/* ── main ─────────────────────────────────────────────────────── */

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_fuzz_url_parse,
		"prop_fuzz_url_parse", 5000, 1);
	failures += property_run(prop_fuzz_proto_decode,
		"prop_fuzz_proto_decode", 5000, 1);
	failures += property_run(prop_fuzz_span_create,
		"prop_fuzz_span_create", 5000, 1);
	failures += property_run(prop_fuzz_traceparent,
		"prop_fuzz_traceparent", 5000, 1);

	if (failures)
		printf("[property] %d fuzz property(ies) FAILED\n",
			failures);
	else
		printf("[property] all fuzz properties passed (no crashes)\n");
	return failures ? 1 : 0;
}
