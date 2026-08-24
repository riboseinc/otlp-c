/* SPDX-License-Identifier: BSD-3-Clause */
/* OTel standard environment-variable configuration — see
 * env_config.h. Parsers are pure; the getenv driver is the only
 * place the environment is read. */
#include "env_config.h"

#include "http_client.h"
#include "internal_util.h"

#include <stdlib.h>
#include <string.h>

/* The OTel base-endpoint form carries no signal path; the traces
 * path is appended per the spec. */
#define OTLP_ENV_TRACES_PATH "/v1/traces"

otlp_status_t
otlp_env_apply_endpoint(otlp_exporter_opts_t *opts,
	const char *value,
	char *buf,
	size_t buf_cap)
{
	size_t len;
	size_t scheme_host_len;
	const char *path_start;
	struct otlp_http_url parsed;
	int n;

	if (!opts || !buf)
		return OTLP_ERR_NULL;
	if (!value || value[0] == '\0')
		return OTLP_OK;

	len = strlen(value);
	/* Find where the path would start: after "://". */
	path_start = strstr(value, "://");
	if (!path_start)
		return OTLP_ERR_INVALID_ARGUMENT;
	scheme_host_len = (size_t)(path_start - value) + 3;
	path_start = value + scheme_host_len;
	while (*path_start != '\0' && *path_start != '/')
		path_start++;

	if (*path_start == '\0')
	{
		/* Base form: append the traces path. */
		if (len + sizeof(OTLP_ENV_TRACES_PATH) > buf_cap)
			return OTLP_ERR_OVERFLOW;
		memcpy(buf, value, len);
		memcpy(buf + len,
			OTLP_ENV_TRACES_PATH,
			sizeof(OTLP_ENV_TRACES_PATH));
	}
	else
	{
		/* Full path already present: copy verbatim. */
		if (len + 1 > buf_cap)
			return OTLP_ERR_OVERFLOW;
		memcpy(buf, value, len + 1);
	}

	/* Validate before committing: create() would parse again,
	 * but rejecting garbage here gives the caller an immediate,
	 * attributable error. */
	n = otlp_http_parse_url(buf, &parsed);
	if (n != OTLP_OK)
		return n;
	opts->endpoint = buf;
	return OTLP_OK;
}

otlp_status_t
otlp_env_apply_traces_endpoint(otlp_exporter_opts_t *opts,
	const char *value,
	char *buf,
	size_t buf_cap)
{
	struct otlp_http_url parsed;
	int n;

	if (!opts || !buf)
		return OTLP_ERR_NULL;
	if (!value || value[0] == '\0')
		return OTLP_OK;

	n = otlp_http_parse_url(value, &parsed);
	if (n != OTLP_OK)
		return n;
	if (strlen(value) + 1 > buf_cap)
		return OTLP_ERR_OVERFLOW;
	memcpy(buf, value, strlen(value) + 1);
	opts->endpoint = buf;
	return OTLP_OK;
}

otlp_status_t
otlp_env_apply_timeout(otlp_exporter_opts_t *opts, const char *value)
{
	uint64_t ms = 0;
	int digits = 0;

	if (!opts)
		return OTLP_ERR_NULL;
	if (!value || value[0] == '\0')
		return OTLP_OK;

	/* Digits only; the 10-digit cap keeps `ms` far from uint64
	 * overflow and rejects anything non-numeric (CWE-190
	 * discipline, same as Retry-After parsing). */
	while (value[digits] != '\0')
	{
		char c = value[digits];

		if (c < '0' || c > '9' || digits >= 10)
			return OTLP_ERR_INVALID_ARGUMENT;
		ms = ms * 10 + (uint64_t)(c - '0');
		digits++;
	}
	if (digits == 0 || ms == 0 || ms > UINT32_MAX)
		return OTLP_ERR_INVALID_ARGUMENT;

	/* The OTel variable is one request-timeout budget; applied to
	 * both phases (connect and read) of the exchange. */
	opts->connect_timeout_ms = (uint32_t) ms;
	opts->read_timeout_ms = (uint32_t) ms;
	return OTLP_OK;
}

otlp_status_t
otlp_env_apply_protocol(otlp_exporter_opts_t *opts, const char *value)
{
	(void) opts;
	if (!value || value[0] == '\0')
		return OTLP_OK;
	/* This library speaks OTLP/HTTP + protobuf only. An explicit
	 * setting naming anything else is a configuration error the
	 * caller wants to hear about, not silently ignored. */
	if (strcmp(value, "http/protobuf") != 0)
		return OTLP_ERR_INVALID_ARGUMENT;
	return OTLP_OK;
}

otlp_status_t
otlp_env_apply_service_name(otlp_exporter_opts_t *opts, const char *value)
{
	if (!opts)
		return OTLP_ERR_NULL;
	if (!value || value[0] == '\0')
		return OTLP_OK;
	/* getenv strings are stable for the process lifetime on every
	 * supported platform; create() deep-copies into the exporter. */
	opts->service_name = value;
	return OTLP_OK;
}

otlp_status_t
otlp_exporter_opts_apply_env(otlp_exporter_opts_t *opts,
	char *buf,
	size_t buf_cap)
{
	otlp_status_t st;

	if (!opts || !buf)
		return OTLP_ERR_NULL;

	st = otlp_env_apply_protocol(
		opts, getenv("OTEL_EXPORTER_OTLP_PROTOCOL"));
	if (st != OTLP_OK)
		return st;
	st = otlp_env_apply_endpoint(
		opts, getenv("OTEL_EXPORTER_OTLP_ENDPOINT"), buf, buf_cap);
	if (st != OTLP_OK)
		return st;
	st = otlp_env_apply_traces_endpoint(opts,
		getenv("OTEL_EXPORTER_OTLP_TRACES_ENDPOINT"),
		buf,
		buf_cap);
	if (st != OTLP_OK)
		return st;
	st = otlp_env_apply_timeout(opts, getenv("OTEL_EXPORTER_OTLP_TIMEOUT"));
	if (st != OTLP_OK)
		return st;
	return otlp_env_apply_service_name(opts, getenv("OTEL_SERVICE_NAME"));
}
