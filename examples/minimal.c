/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Minimal usage example. Demonstrates the full v0.5.x API surface:
 * traces (with events, attributes, sampling), metrics (counter),
 * logs, and context propagation.
 *
 * Uses null_transport mode so it runs without a local otelcol —
 * remove the set_null_transport call to emit to a real collector.
 *
 *   cmake -B build -DOTLP_C_BUILD_EXAMPLES=ON
 *   cmake --build build
 *   ./build/examples/otlp_example_minimal
 */
#include <otlp-c/otlp.h>

#include <stdio.h>

int
main(void)
{
	otlp_exporter_opts_t opts = { 0 };
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	otlp_span_t *span;

	/* ── Exporter ────────────────────────────────────────────────
	 * Zero-initialized opts picks library defaults for endpoint
	 * (http://localhost:4318/v1/traces), batch_size (512),
	 * batch_ms (100), retry/backoff. */
	opts.service_name = "demo-service";
	exp = otlp_exporter_create(&opts);
	if (!exp)
	{
		fprintf(stderr, "exporter create failed\n");
		return 1;
	}
	/* null_transport: skips real HTTP — runs standalone. */
	otlp_exporter_set_null_transport(exp, true);

	/* ── Tracer with sampler ──────────────────────────────────── */
	tracer = otlp_tracer_create("demo-service", "demo", otlp_version());
	if (!tracer)
	{
		otlp_exporter_free(exp);
		return 1;
	}
	/* Always-on sampler is the default; switch to 50% ratio: */
	{
		otlp_sampler_t *s = otlp_sampler_trace_id_ratio_based(0.5);
		otlp_tracer_set_sampler(tracer, s);
	}

	/* ── Span: traces with attributes + events ────────────────── */
	span = otlp_tracer_start_span(tracer, "process-request");
	if (!span)
	{
		/* Sampler dropped this span — not an error. */
		printf("span was sampled out (ratio sampler)\n");
	}
	else
	{
		otlp_span_set_attribute_string(span, "http.method", "GET");
		otlp_span_set_attribute_string(span, "http.route", "/api/data");
		otlp_span_set_attribute_int(span, "http.status", 200);
		/* Attributes are a map: re-setting a key replaces its
		 * value (last write wins, type may change). */
		otlp_span_set_attribute_int(span, "http.status", 201);
		/* Composite (ArrayValue) attribute from otlp_value_t: */
		{
			const otlp_value_t segments[3] = {
				{ .type = OTLP_VALUE_STRING,
					.v = { .string_val = "auth" } },
				{ .type = OTLP_VALUE_STRING,
					.v = { .string_val = "handler" } },
				{ .type = OTLP_VALUE_DOUBLE,
					.v = { .double_val = 0.42 } },
			};
			otlp_span_set_attribute_array(
				span, "pipeline", segments, 3);
		}
		otlp_span_add_event(span, "cache-miss", 0);
		otlp_span_set_event_attribute_string(span, "key", "user_42");
		otlp_span_set_status(span, OTLP_STATUS_CODE_OK, NULL);
		otlp_span_mark_end(span);
		otlp_exporter_emit(exp, span);
		otlp_span_free(span);
	}
	otlp_exporter_flush(exp);

	/* ── Metric: counter ──────────────────────────────────────── */
	{
		otlp_metric_t *m = otlp_metric_create(OTLP_METRIC_COUNTER,
			"requests_total",
			"1",
			"Total HTTP requests",
			NULL,
			0);
		if (m)
		{
			otlp_metric_record(m, 1.0);
			otlp_metric_mark_time(m);
			otlp_metric_set_attribute_string(m, "method", "GET");
			otlp_exporter_flush_metric(exp, m);
			otlp_metric_free(m);
		}
	}

	/* ── Log: structured log record ───────────────────────────── */
	{
		otlp_log_record_t *lr = otlp_log_record_create(
			OTLP_SEVERITY_INFO, "Request completed successfully");
		if (lr)
		{
			otlp_log_record_mark_timestamp(lr);
			otlp_log_record_set_attribute_string(
				lr, "request_id", "req-123");
			otlp_exporter_flush_log(exp, lr);
			otlp_log_record_free(lr);
		}
	}

	/* ── Context propagation ──────────────────────────────────── */
	{
		otlp_span_t *root = otlp_tracer_start_span(tracer, "root");
		if (root)
		{
			otlp_context_t ctx = otlp_context_from_span(root);
			char header[OTLP_TRACEPARENT_BUF_SIZE];
			size_t len;

			if (otlp_traceparent_format(
				    root, true, header, sizeof(header), &len) ==
				OTLP_OK)
				printf("traceparent: %s\n", header);
			(void) ctx;
			otlp_span_free(root);
		}
	}

	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);

	printf("otlp-c %s — demo emitted span + metric + log + context\n",
		otlp_version());
	return 0;
}
