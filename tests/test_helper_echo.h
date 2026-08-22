/* SPDX-License-Identifier-Identifier: Apache-2.0 */
/*
 * In-process HTTP echo server, for tests. Test-only thread; never
 * used by the library itself.
 *
 * accept_loop() binds to 127.0.0.1:0 (kernel-chosen port), listens
 * for N requests, and for each: reads the request, parses the body
 * via Content-Length, returns HTTP 200 with the same body.
 *
 * Optional: a custom handler lets a test return a canned status.
 *
 * Cross-thread state (`running`, `requests_served`, `requests_seen`,
 * `sock_fd`) is atomic so ThreadSanitizer sees clean happens-before
 * edges. The listen fd is CLOSED only by the worker thread;
 * echo_server_stop() sets `stopping` and wakes the worker with a
 * self-connect (shutdown()/close() do not reliably wake a
 * blocked accept() on macOS).
 * The worker thread writes; the test main thread reads via
 * echo_server_join() or directly. Memory ordering: increments and
 * the running=false store use RELEASE; loads use ACQUIRE.
 */
#ifndef OTLP_C_TEST_HELPER_ECHO_H
#define OTLP_C_TEST_HELPER_ECHO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <otlp-c/status.h>
#include "../src/atomic_compat.h"

/* A handler decided by the test. Receives the request body and
 * returns the HTTP status code to send back, and copies the
 * response body into resp_buf (up to *resp_len bytes). */
typedef int (*echo_handler_t)(const uint8_t *req_body,
	size_t req_len,
	uint8_t *resp_buf,
	size_t resp_cap,
	size_t *resp_len);

/* Handler return value: the HTTP status code to respond with, or
 * ECHO_RAW_RESPONSE to send resp_buf[0..*resp_len) as the complete
 * raw response bytes (wire-format parser tests: chunked framing,
 * malformed headers, HTTP/1.0, ...). */
#define ECHO_RAW_RESPONSE (-1)

struct echo_server
{
	uint16_t port;
	otlp_atomic_int
		sock_fd; /* listen fd; ATOMIC: _stop (main
			  * thread) shuts it down while the worker closes it on
			  * exit — single closer: the worker */
	otlp_atomic_int running; /* 0/1; written by worker, polled by main */
	otlp_atomic_int stopping; /* set by _stop; checked by worker
				   * around accept() */
	echo_handler_t handler;
	size_t requests_to_serve; /* const after _start; no sync needed */
	otlp_atomic_u64
		requests_served; /* incremented by worker; read by main */
	otlp_atomic_u64
		requests_seen; /* mirror of requests_served for old callers */
};

/* Start an echo server bound to a kernel-chosen port on localhost.
 * The server thread runs in the background; stop with _stop. The
 * handler may be NULL (default: 200 OK + empty body).
 *
 * `requests_to_serve` is the upper bound on accepted requests; 0
 * means "unbounded" (server runs until _stop). */
otlp_status_t
echo_server_start(struct echo_server *s,
	echo_handler_t handler,
	size_t requests_to_serve);

/* Stop the server: closes the listening socket, waits for the
 * worker thread to exit. Safe to call multiple times. */
void
echo_server_stop(struct echo_server *s);

/* Block until the server has served its allotted requests or until
 * the timeout elapses (microseconds). Returns OTLP_OK on clean
 * shutdown, OTLP_ERR_TIMEOUT if still running. */
otlp_status_t
echo_server_join(struct echo_server *s, uint64_t timeout_us);

#endif
