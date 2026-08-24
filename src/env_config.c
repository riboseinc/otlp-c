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
	otlp_env_storage_t *st)
{
	size_t len;
	size_t scheme_host_len;
	const char *path_start;
	struct otlp_http_url parsed;
	char *buf;
	size_t buf_cap;
	int n;

	if (!opts || !st)
		return OTLP_ERR_NULL;
	buf = st->endpoint;
	buf_cap = sizeof(st->endpoint);
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
	otlp_env_storage_t *st)
{
	struct otlp_http_url parsed;
	int n;

	if (!opts || !st)
		return OTLP_ERR_NULL;
	if (!value || value[0] == '\0')
		return OTLP_OK;

	n = otlp_http_parse_url(value, &parsed);
	if (n != OTLP_OK)
		return n;
	if (strlen(value) + 1 > sizeof(st->endpoint))
		return OTLP_ERR_OVERFLOW;
	memcpy(st->endpoint, value, strlen(value) + 1);
	opts->endpoint = st->endpoint;
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

/* One "k=v" pair as split by the tokenizer. */
struct otlp_env_pair
{
	const char *key;
	size_t key_len;
	const char *val;
	size_t val_len;
};

typedef otlp_status_t (*otlp_env_pair_fn)(const struct otlp_env_pair *, void *);

/* Walk "k=v,k=v" segment by segment. Malformed segments are
 * skipped (OTel's log-and-continue posture); each well-formed
 * pair goes to the callback. Returns its first error, or OK. */
static otlp_status_t
kv_foreach(const char *value, otlp_env_pair_fn cb, void *ctx)
{
	const char *seg = value;

	while (*seg != '\0')
	{
		const char *comma = strchr(seg, ',');
		size_t seg_len = comma ? (size_t)(comma - seg) : strlen(seg);
		const char *eq = memchr(seg, '=', seg_len);

		if (eq != NULL && eq != seg)
		{
			struct otlp_env_pair pair;
			otlp_status_t st;

			pair.key = seg;
			pair.key_len = (size_t)(eq - seg);
			pair.val = eq + 1;
			pair.val_len = seg_len - pair.key_len - 1;
			st = cb(&pair, ctx);
			if (st != OTLP_OK)
				return st;
		}
		seg = comma ? comma + 1 : seg + seg_len;
	}
	return OTLP_OK;
}

struct attrs_ctx
{
	otlp_env_storage_t *st;
};

static otlp_status_t
attrs_one(const struct otlp_env_pair *pair, void *ctx)
{
	otlp_env_storage_t *st = ((struct attrs_ctx *) ctx)->st;
	size_t i = st->n_attrs;

	if (i >= sizeof(st->attrs) / sizeof(st->attrs[0]))
		return OTLP_ERR_OVERFLOW;
	if (pair->key_len >= sizeof(st->attr_keys[0]) ||
		pair->val_len >= sizeof(st->attr_vals[0]))
		return OTLP_ERR_OVERFLOW;
	memcpy(st->attr_keys[i], pair->key, pair->key_len);
	st->attr_keys[i][pair->key_len] = '\0';
	memcpy(st->attr_vals[i], pair->val, pair->val_len);
	st->attr_vals[i][pair->val_len] = '\0';
	st->attrs[i].key = st->attr_keys[i];
	st->attrs[i].value.type = OTLP_VALUE_STRING;
	st->attrs[i].value.v.string_val = st->attr_vals[i];
	st->n_attrs++;
	return OTLP_OK;
}

otlp_status_t
otlp_env_apply_resource_attrs(otlp_exporter_opts_t *opts,
	const char *value,
	otlp_env_storage_t *st)
{
	struct attrs_ctx ctx;
	otlp_status_t st2;

	if (!opts || !st)
		return OTLP_ERR_NULL;
	if (!value || value[0] == '\0')
		return OTLP_OK;

	st->n_attrs = 0;
	ctx.st = st;
	st2 = kv_foreach(value, attrs_one, &ctx);
	if (st2 != OTLP_OK)
		return st2;
	opts->resource_attributes = st->attrs;
	opts->n_resource_attributes = st->n_attrs;
	return OTLP_OK;
}

struct headers_ctx
{
	otlp_env_storage_t *st;
};

static otlp_status_t
headers_one(const struct otlp_env_pair *pair, void *ctx)
{
	otlp_env_storage_t *st = ((struct headers_ctx *) ctx)->st;
	size_t i = st->n_http_headers;

	if (i >= sizeof(st->http_headers) / sizeof(st->http_headers[0]))
		return OTLP_ERR_OVERFLOW;
	if (pair->key_len >= sizeof(st->hdr_names[0]) ||
		pair->val_len >= sizeof(st->hdr_vals[0]))
		return OTLP_ERR_OVERFLOW;
	memcpy(st->hdr_names[i], pair->key, pair->key_len);
	st->hdr_names[i][pair->key_len] = '\0';
	memcpy(st->hdr_vals[i], pair->val, pair->val_len);
	st->hdr_vals[i][pair->val_len] = '\0';
	st->http_headers[i].name = st->hdr_names[i];
	st->http_headers[i].value = st->hdr_vals[i];
	st->n_http_headers++;
	return OTLP_OK;
}

otlp_status_t
otlp_env_apply_otlp_headers(otlp_exporter_opts_t *opts,
	const char *value,
	otlp_env_storage_t *st)
{
	struct headers_ctx ctx;
	otlp_status_t st2;

	if (!opts || !st)
		return OTLP_ERR_NULL;
	if (!value || value[0] == '\0')
		return OTLP_OK;

	st->n_http_headers = 0;
	ctx.st = st;
	st2 = kv_foreach(value, headers_one, &ctx);
	if (st2 != OTLP_OK)
		return st2;
	opts->http_headers = st->http_headers;
	opts->n_http_headers = st->n_http_headers;
	return OTLP_OK;
}

otlp_status_t
otlp_exporter_opts_apply_env(otlp_exporter_opts_t *opts,
	otlp_env_storage_t *storage)
{
	otlp_status_t st;

	if (!opts || !storage)
		return OTLP_ERR_NULL;
	st = otlp_env_apply_protocol(
		opts, getenv("OTEL_EXPORTER_OTLP_PROTOCOL"));
	if (st != OTLP_OK)
		return st;
	st = otlp_env_apply_service_name(opts, getenv("OTEL_SERVICE_NAME"));
	if (st != OTLP_OK)
		return st;

	st = otlp_env_apply_protocol(
		opts, getenv("OTEL_EXPORTER_OTLP_PROTOCOL"));
	if (st != OTLP_OK)
		return st;
	st = otlp_env_apply_endpoint(
		opts, getenv("OTEL_EXPORTER_OTLP_ENDPOINT"), storage);
	if (st != OTLP_OK)
		return st;
	st = otlp_env_apply_traces_endpoint(
		opts, getenv("OTEL_EXPORTER_OTLP_TRACES_ENDPOINT"), storage);
	if (st != OTLP_OK)
		return st;
	st = otlp_env_apply_timeout(opts, getenv("OTEL_EXPORTER_OTLP_TIMEOUT"));
	if (st != OTLP_OK)
		return st;
	st = otlp_env_apply_resource_attrs(
		opts, getenv("OTEL_RESOURCE_ATTRIBUTES"), storage);
	if (st != OTLP_OK)
		return st;
	return otlp_env_apply_otlp_headers(
		opts, getenv("OTEL_EXPORTER_OTLP_HEADERS"), storage);
}
