/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Stub implementations. Replace each function with its real
 * implementation as part of the roadmap phases. See
 * docs/roadmap.md for the order.
 *
 * Stubs return OTLP_ERR_NOT_IMPLEMENTED. The library still links
 * and runs (the example program produces a clean stub run), but
 * no real work happens. This file gets deleted as functions are
 * moved into their real modules.
 */
#include <otlp-c/version.h>
#include <otlp-c/status.h>

const char *
otlp_version(void)
{
	return OTLP_C_VERSION_STRING;
}

const char *
otlp_strerror(otlp_status_t status)
{
	switch (status)
	{
		case OTLP_OK:
			return "OK";
		case OTLP_ERR_INVALID_ARGUMENT:
			return "invalid argument";
		case OTLP_ERR_NOMEM:
			return "out of memory";
		case OTLP_ERR_NULL:
			return "NULL argument";
		case OTLP_ERR_OVERFLOW:
			return "overflow";
		case OTLP_ERR_UTF8:
			return "UTF-8 encoding error";
		case OTLP_ERR_NETWORK:
			return "network error";
		case OTLP_ERR_TIMEOUT:
			return "timeout";
		case OTLP_ERR_DNS:
			return "DNS resolution failed";
		case OTLP_ERR_CONNECT:
			return "connection failed";
		case OTLP_ERR_WRITE:
			return "write failed";
		case OTLP_ERR_READ:
			return "read failed";
		case OTLP_ERR_PROTOCOL:
			return "protocol error";
		case OTLP_ERR_INVALID_RESPONSE:
			return "invalid response";
		case OTLP_ERR_HTTP_STATUS:
			return "unexpected HTTP status";
		case OTLP_ERR_THROTTLED:
			return "throttled by server";
		case OTLP_ERR_SERVER:
			return "server error";
		case OTLP_ERR_BUFFER_FULL:
			return "internal buffer full";
		case OTLP_ERR_SHUTDOWN:
			return "exporter has been shut down";
		case OTLP_ERR_WOULDBLOCK:
			return "operation would block";
		case OTLP_ERR_NOT_IMPLEMENTED:
			return "not implemented yet";
		default:
			return "unknown status";
	}
}
