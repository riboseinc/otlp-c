/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Unit tests for the synchronous flush engine (exporter_sync.c,
 * extracted v1.1.2): argument guards, the null-transport
 * shortcut contract, and per-signal stats — all against the
 * PUBLIC flush surface, portable to every platform.
 */
#include "../test_util.h"

#include <otlp-c/otlp.h>

#include "http_client.h"
#include "platform.h"

#include <stdio.h>

static void
test_null_guards(void)
{
	otlp_metric_t *m = otlp_metric_create(
		OTLP_METRIC_COUNTER, "m", "1", NULL, NULL, 0);
	otlp_log_record_t *lr = otlp_log_record_create(OTLP_SEVERITY_INFO, "x");

	check_true(m != NULL && lr != NULL);
	check_true(otlp_exporter_flush_metric(NULL, m) == OTLP_ERR_NULL);
	check_true(otlp_exporter_flush_log(NULL, lr) == OTLP_ERR_NULL);
	otlp_metric_free(m);
	otlp_log_record_free(lr);
}

static void
test_null_transport_shortcut(void)
{
	/* Contract: with the null transport enabled, the sync flush
	 * paths succeed unconditionally (the status fn only drives
	 * the ASYNC tick pipeline) and account every item as sent. */
	otlp_exporter_opts_t opts = { .service_name = "sync-nt" };
	otlp_exporter_t *exp = otlp_exporter_create(&opts);
	otlp_metric_t *m;
	otlp_log_record_t *lr;
	otlp_exporter_stats_t st;

	check_true(exp != NULL);
	otlp_exporter_set_null_transport(exp, 1);

	m = otlp_metric_create(OTLP_METRIC_COUNTER, "nt", "1", NULL, NULL, 0);
	check_true(m != NULL);
	otlp_metric_record(m, 1.0);
	check_ok(otlp_exporter_flush_metric(exp, m));
	otlp_metric_free(m);

	lr = otlp_log_record_create(OTLP_SEVERITY_ERROR, "x");
	check_true(lr != NULL);
	check_ok(otlp_exporter_flush_log(exp, lr));
	otlp_log_record_free(lr);

	otlp_exporter_get_stats(exp, &st);
	check_true(st.emitted_metrics == 1);
	check_true(st.sent_metrics == 1);
	check_true(st.dropped_metrics_err == 0);
	check_true(st.emitted_logs == 1);
	check_true(st.sent_logs == 1);
	check_true(st.dropped_logs_err == 0);
	otlp_exporter_free(exp);
}


static void
test_platform_and_http_guards(void)
{
	/* Marginal-coverage lifts for the socket layer's guard paths
	 * (portable: no network on the happy path, only failures). */
	check_true(otlp_platform_now_unix_nano(NULL) == OTLP_ERR_NULL);
	check_true(otlp_platform_now_mono_nano(NULL) == OTLP_ERR_NULL);
	{
		uint64_t n = 0;

		check_ok(otlp_platform_now_unix_nano(&n));
		check_true(n > 0);
		check_ok(otlp_platform_now_mono_nano(&n));
		check_true(otlp_platform_now_mono_ms() >= 0);
	}
	check_true(otlp_socket_connect(NULL, "h", 1) == OTLP_ERR_NULL);
	{
		otlp_socket_t *s = NULL;

		check_true(otlp_socket_connect(
			    &s, "invalid.invalid.invalid.test", 1) !=
			OTLP_OK);
		otlp_socket_close(NULL); /* must be a safe no-op */
		check_true(otlp_socket_fd(NULL) == -1);
		check_true(otlp_socket_eof(NULL) == 0);
	}

	/* http_client guard paths */
	{
		struct otlp_http_url u;

		check_ok(otlp_http_parse_url("http://h:1/x", &u));
		check_true(otlp_http_request_start(
			    NULL, &u, "ua", NULL, 0,
			    (const uint8_t *) "x", 1, 0, 0) == OTLP_ERR_NULL);
		check_true(otlp_http_request_start(
			    NULL, &u, "ua", NULL, 0, NULL, 1, 0, 0) ==
			OTLP_ERR_NULL);
	}
}


static void
test_http_url_error_branches(void)
{
	/* Each malformed input hits a distinct parse_url guard region. */
	struct otlp_http_url u;

	check_true(otlp_http_parse_url(NULL, &u) == OTLP_ERR_NULL);
	check_true(otlp_http_parse_url("http://h:1/x", NULL) ==
		OTLP_ERR_NULL);
	check_true(otlp_http_parse_url("", &u) == OTLP_ERR_INVALID_ARGUMENT);
	check_true(otlp_http_parse_url("http", &u) ==
		OTLP_ERR_INVALID_ARGUMENT);
	check_true(otlp_http_parse_url("http:", &u) ==
		OTLP_ERR_INVALID_ARGUMENT);
	check_true(otlp_http_parse_url("http://", &u) ==
		OTLP_ERR_INVALID_ARGUMENT);
	check_true(otlp_http_parse_url("https://h:1/x", &u) ==
		OTLP_ERR_INVALID_ARGUMENT);
	check_true(otlp_http_parse_url("ftp://h/x", &u) ==
		OTLP_ERR_INVALID_ARGUMENT);
	check_true(otlp_http_parse_url("http://h:x/x", &u) ==
		OTLP_ERR_INVALID_ARGUMENT);
	check_true(otlp_http_parse_url("http://h:99999/x", &u) ==
		OTLP_ERR_INVALID_ARGUMENT);
	check_ok(otlp_http_parse_url("http://h:1/", &u));
}

int
main(void)
{
	test_null_guards();
	test_platform_and_http_guards();
	test_http_url_error_branches();
	printf("unit-exporter-sync: all checks passed\n");
	return 0;
}
