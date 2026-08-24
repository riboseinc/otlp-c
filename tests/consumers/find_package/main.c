/* SPDX-License-Identifier: Apache-2.0 */
/* Consumer smoke test: link against the INSTALLED otlp-c and run
 * a real emit -> flush round trip (null transport, no
 * network). */
#include <otlp-c/otlp.h>

#include <stdio.h>

int
main(void)
{
	otlp_exporter_opts_t opts = { .service_name = "consumer" };
	otlp_exporter_t *exp = otlp_exporter_create(&opts);
	otlp_tracer_t *tr = otlp_tracer_create("c", "c", "0");
	otlp_span_t *s;

	if (!exp || !tr)
	{
		return 1;
	}
	otlp_exporter_set_null_transport(exp, 1);

	s = otlp_tracer_start_span(tr, "find_package");
	otlp_span_mark_end(s);
	otlp_exporter_emit(exp, s);
	otlp_span_free(s);

	if (otlp_exporter_flush(exp) != OTLP_OK)
	{
		return 1;
	}

	otlp_tracer_free(tr);
	otlp_exporter_free(exp);
	printf("otlp-c %s round-trip ok\n", otlp_version());
	return 0;
}
