/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Property tests for otlp_http_parse_url.
 *
 *   prop_url_valid_round_trip — parse + reconstruct canonical form.
 *   prop_url_invalid_scheme   — non-http:// rejected.
 *   prop_url_invalid_port     — port > 65535 rejected.
 *   prop_url_default_port     — bare host gets port 80.
 *   prop_url_default_path     — bare host gets path "/".
 */
#include "prng.h"
#include "property_harness.h"

#include "../src/http_client.h"

#include <otlp-c/status.h>

#include <stdio.h>
#include <string.h>

static int
prop_url_valid_round_trip(uint64_t seed)
{
	struct prng p;
	struct otlp_http_url u;
	char url[512];
	char host[64];
	uint16_t port;
	char path[64];
	size_t hlen, plen;
	char *at;
	int ok = 0;
	otlp_status_t st;

	prng_seed(&p, seed);
	hlen = (size_t) prng_u32(&p, 60) + 1;
	for (size_t i = 0; i < hlen; i++)
		host[i] = (char) ('a' + (prng_next(&p) % 26));
	host[hlen] = '\0';

	port = (uint16_t) prng_u32(&p, 65535) + 1;
	plen = (size_t) prng_u32(&p, 60) + 1;
	path[0] = '/';
	for (size_t i = 1; i < plen; i++)
		path[i] = (char) ('a' + (prng_next(&p) % 26));
	path[plen] = '\0';

	snprintf(url,
		sizeof(url),
		"http://%s:%u%s",
		host,
		(unsigned) port,
		path);

	st = otlp_http_parse_url(url, &u);
	if (st != OTLP_OK)
		return 0;

	if (!str_eq(u.host, host))
		return 0;
	if (u.port != port)
		return 0;
	if (!str_eq(u.path, path))
		return 0;
	(void) at;
	ok = 1;
	return ok;
}

static int
prop_url_invalid_scheme(uint64_t seed)
{
	struct otlp_http_url u;

	(void) seed;
	if (otlp_http_parse_url("https://example.com/", &u) !=
		OTLP_ERR_INVALID_ARGUMENT)
		return 0;
	if (otlp_http_parse_url("ftp://example.com/", &u) !=
		OTLP_ERR_INVALID_ARGUMENT)
		return 0;
	if (otlp_http_parse_url("example.com/", &u) !=
		OTLP_ERR_INVALID_ARGUMENT)
		return 0;
	return 1;
}

static int
prop_url_invalid_port(uint64_t seed)
{
	struct otlp_http_url u;

	(void) seed;
	if (otlp_http_parse_url("http://example.com:99999/", &u) !=
		OTLP_ERR_INVALID_ARGUMENT)
		return 0;
	if (otlp_http_parse_url("http://example.com:abc/", &u) !=
		OTLP_ERR_INVALID_ARGUMENT)
		return 0;
	if (otlp_http_parse_url("http://example.com:/", &u) !=
		OTLP_ERR_INVALID_ARGUMENT)
		return 0;
	return 1;
}

static int
prop_url_default_port(uint64_t seed)
{
	struct otlp_http_url u;

	(void) seed;
	if (otlp_http_parse_url("http://example.com/path", &u) != OTLP_OK)
		return 0;
	if (u.port != 80)
		return 0;
	if (!str_eq(u.host, "example.com"))
		return 0;
	if (!str_eq(u.path, "/path"))
		return 0;
	return 1;
}

static int
prop_url_default_path(uint64_t seed)
{
	struct otlp_http_url u;

	(void) seed;
	if (otlp_http_parse_url("http://example.com", &u) != OTLP_OK)
		return 0;
	if (!str_eq(u.path, "/"))
		return 0;
	return 1;
}

/* Regression (v0.5.52): user_agent containing CR/LF must be
 * rejected by otlp_http_request_start. Without this, a caller-
 * controlled user_agent could inject arbitrary HTTP headers
 * via build_request's snprintf into the User-Agent line. */
static int
prop_user_agent_rejects_crlf(uint64_t seed)
{
	struct otlp_http_url u;
	otlp_http_request_t *req = NULL;
	otlp_status_t	     st;

	(void) seed;
	if (otlp_http_parse_url("http://example.com/v1/traces", &u) != OTLP_OK)
		return 0;
	/* CRLF injection attempt. */
	st = otlp_http_request_start(&req, &u, "evil\r\nX-Inject: yes", NULL, 0,
				     (const uint8_t *) "x", 1, 0, 0);
	if (st != OTLP_ERR_INVALID_ARGUMENT)
	{
		if (req)
			otlp_http_request_free(req);
		return 0;
	}
	/* LF only. */
	st = otlp_http_request_start(&req, &u, "evil\nX-Inject: yes", NULL, 0,
				     (const uint8_t *) "x", 1, 0, 0);
	if (st != OTLP_ERR_INVALID_ARGUMENT)
	{
		if (req)
			otlp_http_request_free(req);
		return 0;
	}
	/* Valid user_agent still works. */
	st = otlp_http_request_start(&req, &u, "otlp-c/0.5.52", NULL, 0,
				     (const uint8_t *) "x", 1, 0, 0);
	if (st != OTLP_OK)
		return 0;
	if (req)
		otlp_http_request_free(req);
	return 1;
}

/* Regression (v0.5.52): URLs containing CR/LF must be rejected.
 * Without this, a caller-controlled URL could inject arbitrary
 * HTTP headers into the request line / Host header. */
static int
prop_url_rejects_crlf(uint64_t seed)
{
	struct otlp_http_url u;

	(void) seed;
	/* CRLF in host. */
	if (otlp_http_parse_url("http://evil\r\nx/", &u) !=
		OTLP_ERR_INVALID_ARGUMENT)
		return 0;
	/* CRLF in path. */
	if (otlp_http_parse_url("http://example.com/p\r\nx", &u) !=
		OTLP_ERR_INVALID_ARGUMENT)
		return 0;
	/* LF only. */
	if (otlp_http_parse_url("http://evil\nx/", &u) !=
		OTLP_ERR_INVALID_ARGUMENT)
		return 0;
	/* CR only. */
	if (otlp_http_parse_url("http://evil\rx/", &u) !=
		OTLP_ERR_INVALID_ARGUMENT)
		return 0;
	return 1;
}

/* ── main ─────────────────────────────────────────────────────── */

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_url_valid_round_trip,
		"prop_url_valid_round_trip",
		1000,
		1);
	failures += property_run(
		prop_url_invalid_scheme, "prop_url_invalid_scheme", 1, 1);
	failures += property_run(
		prop_url_invalid_port, "prop_url_invalid_port", 1, 1);
	failures += property_run(
		prop_url_default_port, "prop_url_default_port", 1, 1);
	failures += property_run(
		prop_url_default_path, "prop_url_default_path", 1, 1);
	failures += property_run(
		prop_url_rejects_crlf, "prop_url_rejects_crlf", 1, 1);
	failures += property_run(
		prop_user_agent_rejects_crlf, "prop_user_agent_rejects_crlf", 1, 1);

	if (failures)
		printf("[property] %d url property(ies) failed\n", failures);
	else
		printf("[property] all url properties passed\n");

	return failures ? 1 : 0;
}
