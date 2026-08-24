/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * OTel standard environment-variable configuration (v0.7.0).
 *
 * Names follow the OpenTelemetry environment-variable
 * specification; the parsing lives in PURE helpers (value string
 * in, opts field out — unit-tested without touching the process
 * environment), and one getenv driver applies them. Only
 * variables that are SET (non-empty) overwrite the corresponding
 * opt; everything else passes through untouched.
 *
 * Supported:
 *   OTEL_EXPORTER_OTLP_ENDPOINT         base endpoint (path
 *                                       appended per spec when
 *                                       the value carries none)
 *   OTEL_EXPORTER_OTLP_TRACES_ENDPOINT  full traces endpoint
 *                                       (wins over the base form)
 *   OTEL_EXPORTER_OTLP_TIMEOUT          request timeout, ms
 *   OTEL_EXPORTER_OTLP_PROTOCOL         must be "http/protobuf"
 *   OTEL_SERVICE_NAME                   service name
 *
 * Not supported (documented in README): OTEL_EXPORTER_OTLP_HEADERS
 * and the per-signal metric/log endpoint/timeout variables — the
 * library derives those paths from the one traces endpoint.
 */
#ifndef OTLP_ENV_CONFIG_H
#define OTLP_ENV_CONFIG_H

#include <otlp-c/exporter.h>

#include <stddef.h>

/* Pure parsers. `value` is the variable's content (NULL or ""
 * means unset: no-op, OTLP_OK). Malformed values return
 * OTLP_ERR_INVALID_ARGUMENT and leave opts untouched. */
otlp_status_t
otlp_env_apply_endpoint(otlp_exporter_opts_t *opts,
	const char *value,
	char *buf,
	size_t buf_cap);
otlp_status_t
otlp_env_apply_traces_endpoint(otlp_exporter_opts_t *opts,
	const char *value,
	char *buf,
	size_t buf_cap);
otlp_status_t
otlp_env_apply_timeout(otlp_exporter_opts_t *opts, const char *value);
otlp_status_t
otlp_env_apply_protocol(otlp_exporter_opts_t *opts, const char *value);
otlp_status_t
otlp_env_apply_service_name(otlp_exporter_opts_t *opts, const char *value);

/* The getenv driver over these helpers is public:
 * otlp_exporter_opts_apply_env() in include/otlp-c/exporter.h. */

#endif /* OTLP_ENV_CONFIG_H */
