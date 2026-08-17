/* SPDX-License-Identifier: Apache-2.0 */
/*
 * HTTP response-parser unit tests. Serves raw canned responses
 * (ECHO_RAW_RESPONSE mode) through the echo helper and drives the
 * client state machine:
 *   - Transfer-Encoding: chunked (single chunk, multi-chunk +
 *     trailers, byte-split arrival)
 *   - smuggling vectors rejected (TE + CL, duplicate differing CL)
 *   - undecodable Transfer-Encoding rejected
 *   - line-aligned header matching (a "Content-Length:" inside
 *     another header's value must not match)
 *   - version-aware keep-alive default (HTTP/1.0 -> not reusable)
 *   - case-insensitive header names
 */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test_helper_echo.h"

#include "../src/http_client.h"

#include <otlp-c/status.h>

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
int
main(void)
{
	printf("[http-parser] skipped on Windows\n");
	return 0;
}
#else

/* The canned raw response, set per test. */
static const char *g_raw;

static int
raw_handler(const uint8_t *req_body,
	size_t req_len,
	uint8_t *resp_buf,
	size_t resp_cap,
	size_t *resp_len)
{
	size_t len = strlen(g_raw);

	(void) req_body;
	(void) req_len;
	assert(len <= resp_cap);
	memcpy(resp_buf, g_raw, len);
	*resp_len = len;
	return ECHO_RAW_RESPONSE;
}

static otlp_status_t
drive(otlp_http_request_t *req)
{
	for (int i = 0; i < 100000; i++)
	{
		otlp_status_t st = otlp_http_request_step(req);

		if (otlp_http_request_state(req) == OTLP_HTTP_REQ_DONE)
			return OTLP_OK;
		if (otlp_http_request_state(req) == OTLP_HTTP_REQ_FAILED)
			return st != OTLP_OK ? st : OTLP_ERR_NETWORK;
		if (st == OTLP_ERR_WOULDBLOCK)
		{
			struct timespec ts = { 0, 1000 };

			nanosleep(&ts, NULL);
		}
	}
	return OTLP_ERR_TIMEOUT;
}

static otlp_http_request_t *
run_raw(const char *raw,
	otlp_status_t *st_out,
	int *status_out,
	const uint8_t **body_out,
	size_t *body_len_out)
{
	struct echo_server srv;
	otlp_status_t st;
	struct otlp_http_url url;
	otlp_http_request_t *req = NULL;
	char url_str[128];

	g_raw = raw;
	st = echo_server_start(&srv, raw_handler, 1);
	assert(st == OTLP_OK);
	snprintf(url_str,
		sizeof(url_str),
		"http://127.0.0.1:%u/v1/traces",
		srv.port);
	assert(otlp_http_parse_url(url_str, &url) == OTLP_OK);
	st = otlp_http_request_start(
		&req, &url, "otlp-c/test", (const uint8_t *) "x", 1, 0, 0);
	assert(st == OTLP_OK);
	st = drive(req);
	*st_out = st;
	*status_out = otlp_http_request_http_status(req);
	*body_out = otlp_http_request_body(req, body_len_out);
	(void) echo_server_join(&srv, 2 * 1000 * 1000);
	return req;
}

static int
test_chunked_single(void)
{
	otlp_status_t st;
	int status;
	const uint8_t *body;
	size_t body_len;
	otlp_http_request_t *req = run_raw("HTTP/1.1 200 OK\r\n"
					   "Transfer-Encoding: chunked\r\n"
					   "\r\n"
					   "4\r\nabcd\r\n"
					   "0\r\n\r\n",
		&st,
		&status,
		&body,
		&body_len);

	assert(st == OTLP_OK);
	assert(status == 200);
	assert(body_len == 4 && memcmp(body, "abcd", 4) == 0);
	otlp_http_request_free(req);
	return 0;
}

static int
test_chunked_multi_with_trailers(void)
{
	otlp_status_t st;
	int status;
	const uint8_t *body;
	size_t body_len;
	otlp_http_request_t *req = run_raw("HTTP/1.1 200 OK\r\n"
					   "Transfer-Encoding: chunked\r\n"
					   "\r\n"
					   "5\r\nhello\r\n"
					   "1;ext=1\r\n \r\n"
					   "6\r\n world\r\n"
					   "0\r\n"
					   "X-Checksum: abc\r\n"
					   "\r\n",
		&st,
		&status,
		&body,
		&body_len);

	assert(st == OTLP_OK);
	assert(status == 200);
	assert(body_len == 12);
	assert(memcmp(body, "hello  world", 12) == 0);
	otlp_http_request_free(req);
	return 0;
}

static int
test_chunked_incremental(void)
{
	/* A chunk larger than one socket read usually lands in one
	 * recv; emulate incremental arrival by making the body large
	 * enough that the kernel delivers it in pieces. */
	static char raw[4096];
	otlp_status_t st;
	int status;
	const uint8_t *body;
	size_t body_len;
	size_t i;
	size_t n = 3000;
	otlp_http_request_t *req;

	strcpy(raw,
		"HTTP/1.1 200 OK\r\n"
		"Transfer-Encoding: chunked\r\n"
		"\r\n");
	sprintf(raw + strlen(raw), "%zx\r\n", n);
	for (i = 0; i < n; i++)
		raw[strlen(raw)] = (char) ('a' + (i % 26));
	strcat(raw, "\r\n0\r\n\r\n");

	req = run_raw(raw, &st, &status, &body, &body_len);
	assert(st == OTLP_OK);
	assert(body_len == n);
	assert(body[0] == 'a' && body[n - 1] == (char) ('a' + ((n - 1) % 26)));
	otlp_http_request_free(req);
	return 0;
}

static int
test_te_and_cl_rejected(void)
{
	otlp_status_t st;
	int status;
	const uint8_t *body;
	size_t body_len;
	otlp_http_request_t *req = run_raw("HTTP/1.1 200 OK\r\n"
					   "Content-Length: 2\r\n"
					   "Transfer-Encoding: chunked\r\n"
					   "\r\n"
					   "2\r\nhi\r\n0\r\n\r\n",
		&st,
		&status,
		&body,
		&body_len);

	assert(st != OTLP_OK);
	assert(otlp_http_request_state(req) == OTLP_HTTP_REQ_FAILED);
	otlp_http_request_free(req);
	return 0;
}

static int
test_duplicate_cl_rejected(void)
{
	otlp_status_t st;
	int status;
	const uint8_t *body;
	size_t body_len;
	otlp_http_request_t *req = run_raw("HTTP/1.1 200 OK\r\n"
					   "Content-Length: 2\r\n"
					   "Content-Length: 99\r\n"
					   "\r\n"
					   "hi",
		&st,
		&status,
		&body,
		&body_len);

	assert(st != OTLP_OK);
	otlp_http_request_free(req);
	return 0;
}

static int
test_undecodable_te_rejected(void)
{
	otlp_status_t st;
	int status;
	const uint8_t *body;
	size_t body_len;
	otlp_http_request_t *req = run_raw("HTTP/1.1 200 OK\r\n"
					   "Transfer-Encoding: gzip\r\n"
					   "\r\n"
					   "....",
		&st,
		&status,
		&body,
		&body_len);

	assert(st != OTLP_OK);
	otlp_http_request_free(req);
	return 0;
}

static int
test_header_value_not_matched(void)
{
	/* "Content-Length: 99" appears inside another header's VALUE —
	 * a substring scan would wait for 99 body bytes and time out;
	 * the line-aligned scan reads the real CL. */
	otlp_status_t st;
	int status;
	const uint8_t *body;
	size_t body_len;
	otlp_http_request_t *req =
		run_raw("HTTP/1.1 200 OK\r\n"
			"X-Note: see Content-Length: 99 for details\r\n"
			"Content-Length: 2\r\n"
			"\r\n"
			"hi",
			&st,
			&status,
			&body,
			&body_len);

	assert(st == OTLP_OK);
	assert(status == 200);
	assert(body_len == 2 && memcmp(body, "hi", 2) == 0);
	otlp_http_request_free(req);
	return 0;
}

static int
test_http10_not_reusable(void)
{
	otlp_status_t st;
	int status;
	const uint8_t *body;
	size_t body_len;
	otlp_http_request_t *req = run_raw("HTTP/1.0 200 OK\r\n"
					   "Content-Length: 2\r\n"
					   "\r\n"
					   "hi",
		&st,
		&status,
		&body,
		&body_len);

	assert(st == OTLP_OK);
	assert(status == 200);
	/* HTTP/1.0 defaults to close: no detachable socket. */
	assert(otlp_http_request_detach_socket(req) == NULL);
	otlp_http_request_free(req);
	return 0;
}

static int
test_case_insensitive_headers(void)
{
	otlp_status_t st;
	int status;
	const uint8_t *body;
	size_t body_len;
	otlp_http_request_t *req = run_raw("HTTP/1.1 204 No Content\r\n"
					   "cOnTeNt-LeNgTh: 0\r\n"
					   "\r\n",
		&st,
		&status,
		&body,
		&body_len);

	assert(st == OTLP_OK);
	assert(status == 204);
	assert(body_len == 0);
	otlp_http_request_free(req);
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_chunked_single();
	failures += test_chunked_multi_with_trailers();
	failures += test_chunked_incremental();
	failures += test_te_and_cl_rejected();
	failures += test_duplicate_cl_rejected();
	failures += test_undecodable_te_rejected();
	failures += test_header_value_not_matched();
	failures += test_http10_not_reusable();
	failures += test_case_insensitive_headers();

	if (failures)
		printf("[http-parser] FAIL (%d test(s))\n", failures);
	else
		printf("[http-parser] PASS (9 tests)\n");
	return failures ? 1 : 0;
}

#endif
