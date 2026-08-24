/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Property test for HTTP connect timeout enforcement.
 *
 *   prop_connect_timeout_fires — a request to an unreachable
 *     endpoint with connect_timeout_ms set reaches FAILED within
 *     a bounded window, rather than hanging forever.
 *
 * Uses 192.0.2.1 (RFC 5737 TEST-NET-1, reserved for documentation,
 * guaranteed by IANA to never be routed). On typical systems the
 * non-blocking connect() returns EINPROGRESS (SYN sent, no response),
 * and the step function's deadline check transitions the request to
 * FAILED after connect_timeout_ms elapses.
 *
 * On systems where the SYN is immediately rejected (no route to host,
 * network unreachable), the request fails instantly — also acceptable
 * behavior. And on VPN/proxy networks that locally accept every TCP
 * connect, the connection SUCCEEDS, the request advances to READING,
 * and the read/inactivity deadline terminates it — equally acceptable.
 * The test verifies BOUNDED completion in every case; the wall-clock
 * cap sits well above connect+read deadlines so the read-deadline
 * path passes too (a cap equal to read_timeout made this property
 * fail deterministically on VPN networks, where connect always
 * succeeds).
 *
 * POSIX-only (uses clock_gettime for timing).
 */
#include "property_harness.h"

#include "../src/http_client.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
int
main(void)
{
	printf("[property] http-timeout test skipped on Windows\n");
	return 0;
}
#else

#include <time.h>

static uint64_t
mono_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000ULL +
	       (uint64_t) ts.tv_nsec / 1000000ULL;
}

static int
prop_connect_timeout_fires(uint64_t seed)
{
	struct otlp_http_url url;
	otlp_http_request_t *req = NULL;
	otlp_status_t       st;
	uint64_t             t0, t1, elapsed;
	int                  ok = 0;
	int                  i;

	(void) seed;

	/* 192.0.2.1 — RFC 5737 TEST-NET-1, IANA-reserved, never routed. */
	if (otlp_http_parse_url("http://192.0.2.1:4318/v1/traces", &url) != OTLP_OK)
		return 0;

	st = otlp_http_request_start(&req, &url, "timeout-test",
				      NULL, 0,
				      (const uint8_t *) "x", 1,
				      200,  /* connect_timeout_ms */
				      2000  /* read_timeout_ms (generous) */);
	if (st != OTLP_OK)
		return 0;

	t0 = mono_ms();
	for (i = 0; i < 10000; i++) {
		otlp_http_req_state_t s;

		(void) otlp_http_request_step(req);
		s = otlp_http_request_state(req);
		if (s == OTLP_HTTP_REQ_DONE || s == OTLP_HTTP_REQ_FAILED) {
			t1 = mono_ms();
			elapsed = t1 - t0;
			/* Bounded completion: the request reached a terminal
			 * state. If it timed out (Case A), elapsed should be
			 * near 200ms. If it failed instantly (Case B — no
			 * route to host), elapsed is near 0. If the connect
			 * was locally accepted by a VPN/proxy (Case C), the
			 * read deadline fires near 2000ms. All acceptable;
			 * what matters is it didn't hang forever.
			 *
			 * Cap: 5000ms — well past connect+read deadlines. If
			 * it takes longer, a timeout didn't fire. */
			ok = (elapsed < 5000);
			break;
		}
		struct timespec ts = { 0, 1000 * 1000 }; /* 1ms */
		nanosleep(&ts, NULL);
	}

	otlp_http_request_free(req);

	if (!ok) {
		t1 = mono_ms();
		elapsed = t1 - t0;
		fprintf(stderr, "[property] connect timeout FAILED: elapsed=%llums\n",
			(unsigned long long) elapsed);
	}
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_connect_timeout_fires,
				 "prop_connect_timeout_fires", 3, 1);

	if (failures)
		printf("[property] %d http-timeout property(ies) failed\n",
		       failures);
	else
		printf("[property] all http-timeout properties passed\n");
	return failures ? 1 : 0;
}

#endif /* !_WIN32 */
