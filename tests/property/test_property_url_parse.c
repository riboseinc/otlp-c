/* SPDX-License-Identifier: Apache-2.0 */
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

	if (failures)
		printf("[property] %d url property(ies) failed\n", failures);
	else
		printf("[property] all url properties passed\n");

	return failures ? 1 : 0;
}
