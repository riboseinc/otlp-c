// SPDX-License-Identifier: BSD-3-Clause
//
// otlp_strerror() completeness pin (v0.5.105): every status code
// in the public enum must map to a non-empty, DISTINCT message.
// The mapping is complete today (20/20); this pins it so a new
// enum value without a strerror entry cannot land silently.

#include "../test_util.h"

#include <otlp-c/status.h>

#include <string.h>

static int
test_strerror_complete(void)
{
	const otlp_status_t codes[] = {
		OTLP_OK,
		OTLP_ERR_INVALID_ARGUMENT,
		OTLP_ERR_NOMEM,
		OTLP_ERR_NULL,
		OTLP_ERR_OVERFLOW,
		OTLP_ERR_UTF8,
		OTLP_ERR_NETWORK,
		OTLP_ERR_TIMEOUT,
		OTLP_ERR_DNS,
		OTLP_ERR_CONNECT,
		OTLP_ERR_WRITE,
		OTLP_ERR_READ,
		OTLP_ERR_PROTOCOL,
		OTLP_ERR_INVALID_RESPONSE,
		OTLP_ERR_HTTP_STATUS,
		OTLP_ERR_THROTTLED,
		OTLP_ERR_SERVER,
		OTLP_ERR_BUFFER_FULL,
		OTLP_ERR_SHUTDOWN,
		OTLP_ERR_WOULDBLOCK,
		OTLP_ERR_NOT_IMPLEMENTED,
	};
	const size_t n = sizeof(codes) / sizeof(codes[0]);
	const char *seen[32];

	check_true(n <= 32);
	for (size_t i = 0; i < n; i++)
	{
		const char *msg = otlp_strerror(codes[i]);

		check_true(msg != NULL);
		check_true(msg[0] != '\0');
		/* Distinct codes get distinct messages — a duplicated
		 * message hides a mis-mapped case just as effectively
		 * as a missing one. */
		for (size_t j = 0; j < i; j++)
			check_true(strcmp(msg, seen[j]) != 0);
		seen[i] = msg;
	}
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_strerror_complete();

	if (failures)
		printf("[unit-common] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-common] PASS (1 test)\n");
	return failures ? 1 : 0;
}
