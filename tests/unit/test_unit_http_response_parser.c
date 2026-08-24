/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Byte-fixture tests for the HTTP response wire-format parser.
 *
 * Pure bytes-in/verdict-out: no sockets, no threads, no POSIX —
 * runs on every platform including the Windows CI job (which the
 * old socket-driven response fuzz never covered).
 */
#include "../test_util.h"

#include "http_response_parser.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void
feed_expect(const char *resp,
	int want_rc,
	int want_status,
	const char *want_body,
	bool want_keepalive)
{
	uint8_t buf[512];
	size_t len = strlen(resp);
	struct otlp_http_resp out;
	int rc;

	memset(&out, 0, sizeof(out));
	memcpy(buf, resp, len);
	rc = otlp_http_resp_parse(buf, len, false, &out);

	if (want_rc == 1)
	{
		check_true(rc == 1);
		check_true(out.http_status == want_status);
		check_true(out.body_len == strlen(want_body));
		check_true(out.body_len == 0 ||
			memcmp(out.body, want_body, out.body_len) == 0);
		check_true(out.body >= buf &&
			out.body + out.body_len <= buf + len);
		check_true(out.keepalive_eligible == want_keepalive);
	}
	else
	{
		check_true(rc == want_rc);
	}
}

/* Split delivery: same bytes, cut at every offset, must never
 * produce a verdict that the full buffer contradicts. */
static void
feed_split(const char *resp, int want_final_rc, int want_status)
{
	uint8_t buf[512];
	size_t len = strlen(resp);
	struct otlp_http_resp out;
	size_t cut;
	int rc_final = -99;

	/* First find the full-buffer verdict. */
	memcpy(buf, resp, len);
	rc_final = otlp_http_resp_parse(buf, len, false, &out);
	check_true(rc_final == want_final_rc);
	if (want_final_rc == 1)
		check_true(out.http_status == want_status);

	for (cut = 0; cut < len; cut++)
	{
		struct otlp_http_resp part;
		int rc;

		memcpy(buf, resp, len);
		rc = otlp_http_resp_parse(buf, cut, false, &part);
		/* Prefixes may only be incomplete or agree on failure. */
		check_true(rc == 0 || rc == rc_final);
	}
}

int
main(void)
{
	/* ── Status line + Content-Length ─────────────────────────── */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Content-Length: 5\r\n"
		    "\r\n"
		    "hello",
		1,
		200,
		"hello",
		true);
	/* Body not fully arrived yet. */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Content-Length: 5\r\n"
		    "\r\n"
		    "hel",
		0,
		0,
		"",
		false);
	/* Zero-length body. */
	feed_expect("HTTP/1.1 204 No Content\r\n"
		    "Content-Length: 0\r\n"
		    "\r\n",
		1,
		204,
		"",
		true);
	/* Reason phrase containing spaces and another status-looking
	 * token — only the first 3 digits after the space count. */
	feed_expect("HTTP/1.1 404 Not 200 Found\r\n"
		    "Content-Length: 2\r\n"
		    "\r\n"
		    "no",
		1,
		404,
		"no",
		true);
	/* Malformed status line: no digits. */
	feed_expect("HTTP/1.1 abc def\r\n"
		    "Content-Length: 0\r\n"
		    "\r\n",
		-1,
		0,
		"",
		false);
	/* Not HTTP. */
	feed_expect("GARBAGE/1.1 200 OK\r\n"
		    "Content-Length: 0\r\n"
		    "\r\n",
		-1,
		0,
		"",
		false);
	/* Missing terminator. */
	feed_expect(
		"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n", 0, 0, "", false);

	/* ── Split delivery invariance ────────────────────────────── */
	feed_split("HTTP/1.1 200 OK\r\n"
		   "Content-Length: 5\r\n"
		   "\r\n"
		   "hello",
		1,
		200);
	feed_split("HTTP/1.1 500 Oops\r\n"
		   "Transfer-Encoding: chunked\r\n"
		   "\r\n"
		   "5\r\nabcde\r\n0\r\n\r\n",
		1,
		500);

	/* ── Version-aware keep-alive ─────────────────────────────── */
	feed_expect("HTTP/1.0 200 OK\r\n"
		    "Content-Length: 1\r\n"
		    "\r\n"
		    "x",
		1,
		200,
		"x",
		false); /* 1.0 defaults to close */
	feed_expect("HTTP/1.0 200 OK\r\n"
		    "Connection: keep-alive\r\n"
		    "Content-Length: 1\r\n"
		    "\r\n"
		    "x",
		1,
		200,
		"x",
		true); /* header upgrades */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Connection: close\r\n"
		    "Content-Length: 1\r\n"
		    "\r\n"
		    "x",
		1,
		200,
		"x",
		false); /* header overrides */

	/* ── No Content-Length: EOF framing ───────────────────────── */
	{
		uint8_t buf[512];
		const char *r = "HTTP/1.1 200 OK\r\n"
				"\r\n"
				"body-until-close";
		size_t len = strlen(r);
		struct otlp_http_resp out;
		int rc;

		memcpy(buf, r, len);
		rc = otlp_http_resp_parse(buf, len, false, &out);
		check_true(rc == 0); /* no EOF yet: incomplete */

		memcpy(buf, r, len);
		rc = otlp_http_resp_parse(buf, len, true, &out);
		check_true(rc == 1);
		check_true(out.http_status == 200);
		check_true(out.body_len == strlen("body-until-close"));
		check_true(memcmp(out.body, "body-until-close", out.body_len) ==
			0);
		check_true(!out.keepalive_eligible); /* ambiguous framing */
	}

	/* ── Request-smuggling rejections (RFC 7230) ──────────────── */
	/* TE + CL together: classic vector. */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Transfer-Encoding: chunked\r\n"
		    "Content-Length: 5\r\n"
		    "\r\n"
		    "hello",
		-1,
		0,
		"",
		false);
	/* Duplicate CL, DIFFERENT values. */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Content-Length: 5\r\n"
		    "Content-Length: 6\r\n"
		    "\r\n"
		    "hello!",
		-1,
		0,
		"",
		false);
	/* Duplicate CL, IDENTICAL values: legal, collapses. */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Content-Length: 5\r\n"
		    "Content-Length: 5\r\n"
		    "\r\n"
		    "hello",
		1,
		200,
		"hello",
		true);
	/* Undecodable coding. */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Transfer-Encoding: gzip\r\n"
		    "\r\n"
		    "data",
		-1,
		0,
		"",
		false);

	/* ── Line-aligned header matching ─────────────────────────── */
	/* "Content-Length" inside another header's VALUE must not be
	 * read as a framing header (v0.5.52 lesson). */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "X-Note: see Content-Length: 5 not real\r\n"
		    "\r\n"
		    "xx",
		0,
		0,
		"",
		false); /* no real CL, no EOF */

	/* ── Case-insensitive header names ────────────────────────── */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "cOnTeNt-LeNgTh: 4\r\n"
		    "\r\n"
		    "abcd",
		1,
		200,
		"abcd",
		true);

	/* ── Chunked framing ──────────────────────────────────────── */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Transfer-Encoding: chunked\r\n"
		    "\r\n"
		    "5\r\nabcde\r\n0\r\n\r\n",
		1,
		200,
		"abcde",
		true);
	/* Multi-chunk + extensions + trailer. */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Transfer-Encoding: chunked\r\n"
		    "\r\n"
		    "3;ext=1\r\nabc\r\n4\r\ndefg\r\n0\r\n"
		    "X-Trailer: t\r\n"
		    "\r\n",
		1,
		200,
		"abcdefg",
		true);
	/* Partial chunk: incomplete. */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Transfer-Encoding: chunked\r\n"
		    "\r\n"
		    "5\r\nabc",
		0,
		0,
		"",
		false);
	/* Bad chunk terminator. */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Transfer-Encoding: chunked\r\n"
		    "\r\n"
		    "5\r\nabcdeXX0\r\n\r\n",
		-1,
		0,
		"",
		false);
	/* Non-hex chunk size. */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Transfer-Encoding: chunked\r\n"
		    "\r\n"
		    "zz\r\n",
		-1,
		0,
		"",
		false);

	/* ── Retry-After ──────────────────────────────────────────── */
	{
		uint8_t buf[512];
		struct otlp_http_resp out;
		size_t len;
		int rc;

		/* delta-seconds form. */
		len = strlen("HTTP/1.1 429 Too Many\r\n"
			     "Retry-After: 2\r\n"
			     "Content-Length: 0\r\n"
			     "\r\n");
		memcpy(buf,
			"HTTP/1.1 429 Too Many\r\n"
			"Retry-After: 2\r\n"
			"Content-Length: 0\r\n"
			"\r\n",
			len);
		rc = otlp_http_resp_parse(buf, len, false, &out);
		check_true(rc == 1);
		check_true(out.retry_after_ms == 2000);

		/* Huge value: saturates at the uint32 ceiling. */
		len = strlen("HTTP/1.1 429 Too Many\r\n"
			     "Retry-After: 99999999999\r\n"
			     "Content-Length: 0\r\n"
			     "\r\n");
		memcpy(buf,
			"HTTP/1.1 429 Too Many\r\n"
			"Retry-After: 99999999999\r\n"
			"Content-Length: 0\r\n"
			"\r\n",
			len);
		rc = otlp_http_resp_parse(buf, len, false, &out);
		check_true(rc == 1);
		check_true(out.retry_after_ms == 4294967U * 1000U);

		/* HTTP-date form: treated as absent (0). */
		len = strlen("HTTP/1.1 503 Unavailable\r\n"
			     "Retry-After: Wed, 21 Oct 2015 07:28:00 GMT\r\n"
			     "Content-Length: 0\r\n"
			     "\r\n");
		memcpy(buf,
			"HTTP/1.1 503 Unavailable\r\n"
			"Retry-After: Wed, 21 Oct 2015 07:28:00 GMT\r\n"
			"Content-Length: 0\r\n"
			"\r\n",
			len);
		rc = otlp_http_resp_parse(buf, len, false, &out);
		check_true(rc == 1);
		check_true(out.retry_after_ms == 0);
	}

	/* ── Oversized Content-Length: rejected ───────────────────── */
	feed_expect("HTTP/1.1 200 OK\r\n"
		    "Content-Length: 99999999\r\n"
		    "\r\n",
		-1,
		0,
		"",
		false);

	printf("unit-http-response-parser: all checks passed\n");
	return 0;
}
