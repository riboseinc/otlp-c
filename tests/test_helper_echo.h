/* SPDX-License-Identifier: Apache-2.0 */
/*
 * In-process HTTP echo server, for tests. Test-only thread; never
 * used by the library itself.
 *
 * accept_loop() binds to 127.0.0.1:0 (kernel-chosen port), listens
 * for N requests, and for each: reads the request, parses the body
 * via Content-Length, returns HTTP 200 with the same body.
 *
 * Optional: a custom handler lets a test return a canned status.
 */
#ifndef OTLP_C_TEST_HELPER_ECHO_H
#define OTLP_C_TEST_HELPER_ECHO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <otlp-c/status.h>

/* A handler decided by the test. Receives the request body and
 * returns the HTTP status code to send back, and copies the
 * response body into resp_buf (up to *resp_len bytes). */
typedef int (*echo_handler_t)(const uint8_t *req_body,
	size_t req_len,
	uint8_t *resp_buf,
	size_t resp_cap,
	size_t *resp_len);

struct echo_server
{
	uint16_t port;
	int sock_fd;
	bool running;
	echo_handler_t handler;
	size_t requests_to_serve;
	size_t requests_served;
};

/* Start an echo server bound to a kernel-chosen port on localhost.
 * The server thread runs in the background; stop with _join. The
 * handler may be NULL (default: 200 OK + empty body). */
otlp_status_t
echo_server_start(struct echo_server *s,
	echo_handler_t handler,
	size_t requests_to_serve);

/* Block until the server has served its allotted requests or until
 * the timeout elapses (microseconds). Returns OTLP_OK on clean
 * shutdown, OTLP_ERR_TIMEOUT if still running. */
otlp_status_t
echo_server_join(struct echo_server *s, uint64_t timeout_us);

#endif
