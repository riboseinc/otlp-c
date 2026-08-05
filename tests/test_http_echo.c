/* SPDX-License-Identifier: Apache-2.0 */
/*
 * HTTP echo unit test. Drives otlp_http_request_t against an
 * in-process echo server; verifies:
 *   - request body is received by server
 *   - response status line is parsed correctly
 *   - response body is captured intact
 */
#if !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "test_helper_echo.h"

#include "../src/http_client.h"

#include <otlp-c/status.h>

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
int
main(void)
{
	printf("[http-echo] skipped on Windows\n");
	return 0;
}
#else

static int
echo_handler(const uint8_t *req_body,
	size_t req_len,
	uint8_t *resp_buf,
	size_t resp_cap,
	size_t *resp_len)
{
	if (req_len > resp_cap)
		req_len = resp_cap;
	memcpy(resp_buf, req_body, req_len);
	*resp_len = req_len;
	return 200;
}

/* Drive the state machine until it reaches a terminal state or the
 * iteration cap. Returns OTLP_OK on DONE, otherwise the last error. */
static otlp_status_t
drive_to_completion(otlp_http_request_t *req)
{
	for (int i = 0; i < 100000; i++)
	{
		otlp_status_t st = otlp_http_request_step(req);
		otlp_http_req_state_t s = otlp_http_request_state(req);

		if (s == OTLP_HTTP_REQ_DONE)
			return OTLP_OK;
		if (s == OTLP_HTTP_REQ_FAILED)
			return st != OTLP_OK ? st : OTLP_ERR_NETWORK;
		if (st == OTLP_ERR_WOULDBLOCK)
		{
			struct timespec ts = { 0, 1000 /* 1us */ };
			nanosleep(&ts, NULL);
		}
	}
	return OTLP_ERR_TIMEOUT;
}

int
main(void)
{
	struct echo_server srv;
	otlp_status_t st;
	struct otlp_http_url url;
	otlp_http_request_t *req = NULL;
	const char *body = "hello world";
	const uint8_t *resp;
	size_t resp_len;
	char url_str[128];

	st = echo_server_start(&srv, echo_handler, 1);
	assert(st == OTLP_OK);
	snprintf(url_str,
		sizeof(url_str),
		"http://127.0.0.1:%u/v1/traces",
		srv.port);
	st = otlp_http_parse_url(url_str, &url);
	assert(st == OTLP_OK);

	st = otlp_http_request_start(&req,
		&url,
		"otlp-c/test",
		(const uint8_t *) body,
		strlen(body));
	assert(st == OTLP_OK);

	st = drive_to_completion(req);
	assert(st == OTLP_OK);
	assert(otlp_http_request_state(req) == OTLP_HTTP_REQ_DONE);
	assert(otlp_http_request_http_status(req) == 200);

	resp = otlp_http_request_body(req, &resp_len);
	assert(resp_len == strlen(body));
	assert(memcmp(resp, body, resp_len) == 0);

	otlp_http_request_free(req);
	(void) echo_server_join(&srv, 1 * 1000 * 1000);
	printf("[http-echo] PASS — round-trip OK, body echoed\n");
	return 0;
}

#endif
