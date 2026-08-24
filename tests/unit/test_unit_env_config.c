/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Unit tests for the OTel standard environment-variable
 * configuration (v0.7.0). The pure parsers take value STRINGS, so
 * almost everything is testable without touching the process
 * environment; one getenv-driven section uses a portable
 * set/unset wrapper.
 */
#include "../test_util.h"

#include "env_config.h"

#include <otlp-c/otlp.h>

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define SETENV(n, v) _putenv_s(n, v)
#define UNSETENV(n) _putenv_s(n, "")
#else
#include <stdlib.h>
#define SETENV(n, v) setenv(n, v, 1)
#define UNSETENV(n) unsetenv(n)
#endif

static void
test_endpoint(void)
{
	otlp_exporter_opts_t o = { 0 };
	char buf[256];

	/* Unset/empty: no-op. */
	buf[0] = 'x';
	check_ok(otlp_env_apply_endpoint(&o, NULL, buf, sizeof(buf)));
	check_ok(otlp_env_apply_endpoint(&o, "", buf, sizeof(buf)));
	check_true(o.endpoint == NULL);
	check_true(buf[0] == 'x'); /* untouched */

	/* Base form: "/v1/traces" appended. */
	check_ok(otlp_env_apply_endpoint(
		&o, "http://collector:4318", buf, sizeof(buf)));
	check_true(strcmp(o.endpoint, "http://collector:4318/v1/traces") == 0);
	check_true(o.endpoint == buf);

	/* Full path form: kept verbatim. */
	check_ok(otlp_env_apply_endpoint(
		&o, "http://collector:4318/custom/path", buf, sizeof(buf)));
	check_true(
		strcmp(o.endpoint, "http://collector:4318/custom/path") == 0);

	/* Garbage: rejected, opts untouched. */
	o.endpoint = "keep";
	check_true(otlp_env_apply_endpoint(&o, "not-a-url", buf, sizeof(buf)) ==
		OTLP_ERR_INVALID_ARGUMENT);
	check_true(strcmp(o.endpoint, "keep") == 0);

	/* Buffer too small: overflow, opts untouched. */
	check_true(
		otlp_env_apply_endpoint(&o, "http://collector:4318", buf, 16) ==
		OTLP_ERR_OVERFLOW);
	check_true(strcmp(o.endpoint, "keep") == 0);
}

static void
test_traces_endpoint_wins(void)
{
	otlp_exporter_opts_t o = { 0 };
	char buf[256];

	check_ok(otlp_env_apply_endpoint(
		&o, "http://base:4318", buf, sizeof(buf)));
	check_true(strcmp(o.endpoint, "http://base:4318/v1/traces") == 0);
	/* The signal-specific full form overrides the base form. */
	check_ok(otlp_env_apply_traces_endpoint(
		&o, "http://direct:1234/v1/traces?x=1", buf, sizeof(buf)));
	check_true(strcmp(o.endpoint, "http://direct:1234/v1/traces?x=1") == 0);
}

static void
test_timeout(void)
{
	otlp_exporter_opts_t o = { .connect_timeout_ms = 1,
		.read_timeout_ms = 2 };

	check_ok(otlp_env_apply_timeout(&o, NULL));
	check_ok(otlp_env_apply_timeout(&o, ""));
	check_true(o.connect_timeout_ms == 1 && o.read_timeout_ms == 2);

	check_ok(otlp_env_apply_timeout(&o, "15000"));
	check_true(o.connect_timeout_ms == 15000);
	check_true(o.read_timeout_ms == 15000);

	check_true(
		otlp_env_apply_timeout(&o, "0") == OTLP_ERR_INVALID_ARGUMENT);
	check_true(
		otlp_env_apply_timeout(&o, "-5") == OTLP_ERR_INVALID_ARGUMENT);
	check_true(
		otlp_env_apply_timeout(&o, "1s") == OTLP_ERR_INVALID_ARGUMENT);
	check_true(otlp_env_apply_timeout(&o, "12345678901") ==
		OTLP_ERR_INVALID_ARGUMENT); /* 11 digits */
	check_true(otlp_env_apply_timeout(&o, "4294967296") ==
		OTLP_ERR_INVALID_ARGUMENT); /* > UINT32_MAX */
	/* Failed parses left the last good value in place. */
	check_true(o.read_timeout_ms == 15000);
}

static void
test_protocol(void)
{
	otlp_exporter_opts_t o = { 0 };

	check_ok(otlp_env_apply_protocol(&o, NULL));
	check_ok(otlp_env_apply_protocol(&o, ""));
	check_ok(otlp_env_apply_protocol(&o, "http/protobuf"));
	check_true(otlp_env_apply_protocol(&o, "grpc") ==
		OTLP_ERR_INVALID_ARGUMENT);
}

static void
test_service_name(void)
{
	otlp_exporter_opts_t o = { 0 };

	check_ok(otlp_env_apply_service_name(&o, NULL));
	check_true(o.service_name == NULL);
	check_ok(otlp_env_apply_service_name(&o, "checkout"));
	check_true(strcmp(o.service_name, "checkout") == 0);
}

static void
test_getenv_driver(void)
{
	otlp_exporter_opts_t o = { 0 };
	char buf[256];

	UNSETENV("OTEL_EXPORTER_OTLP_PROTOCOL");
	UNSETENV("OTEL_EXPORTER_OTLP_ENDPOINT");
	UNSETENV("OTEL_EXPORTER_OTLP_TRACES_ENDPOINT");
	UNSETENV("OTEL_EXPORTER_OTLP_TIMEOUT");
	UNSETENV("OTEL_SERVICE_NAME");
	check_ok(otlp_exporter_opts_apply_env(&o, buf, sizeof(buf)));
	check_true(o.endpoint == NULL && o.service_name == NULL);

	SETENV("OTEL_EXPORTER_OTLP_ENDPOINT", "http://env-collector:4318");
	SETENV("OTEL_EXPORTER_OTLP_TIMEOUT", "2500");
	SETENV("OTEL_SERVICE_NAME", "from-env");
	check_ok(otlp_exporter_opts_apply_env(&o, buf, sizeof(buf)));
	check_true(
		strcmp(o.endpoint, "http://env-collector:4318/v1/traces") == 0);
	check_true(o.connect_timeout_ms == 2500);
	check_true(o.read_timeout_ms == 2500);
	check_true(strcmp(o.service_name, "from-env") == 0);

	/* TRACES_ENDPOINT beats ENDPOINT. */
	SETENV("OTEL_EXPORTER_OTLP_TRACES_ENDPOINT",
		"http://signal:9999/v1/traces");
	check_ok(otlp_exporter_opts_apply_env(&o, buf, sizeof(buf)));
	check_true(strcmp(o.endpoint, "http://signal:9999/v1/traces") == 0);

	/* A bad protocol fails the whole call. */
	SETENV("OTEL_EXPORTER_OTLP_PROTOCOL", "grpc");
	check_true(otlp_exporter_opts_apply_env(&o, buf, sizeof(buf)) ==
		OTLP_ERR_INVALID_ARGUMENT);
	SETENV("OTEL_EXPORTER_OTLP_PROTOCOL", "http/protobuf");
	check_ok(otlp_exporter_opts_apply_env(&o, buf, sizeof(buf)));

	/* The composed opts must produce a working exporter. */
	{
		otlp_exporter_t *exp = otlp_exporter_create(&o);
		check_true(exp != NULL);
		otlp_exporter_free(exp);
	}

	UNSETENV("OTEL_EXPORTER_OTLP_ENDPOINT");
	UNSETENV("OTEL_EXPORTER_OTLP_TRACES_ENDPOINT");
	UNSETENV("OTEL_EXPORTER_OTLP_TIMEOUT");
	UNSETENV("OTEL_SERVICE_NAME");
	UNSETENV("OTEL_EXPORTER_OTLP_PROTOCOL");
}

int
main(void)
{
	test_endpoint();
	test_traces_endpoint_wins();
	test_timeout();
	test_protocol();
	test_service_name();
	test_getenv_driver();
	printf("unit-env-config: all checks passed\n");
	return 0;
}
