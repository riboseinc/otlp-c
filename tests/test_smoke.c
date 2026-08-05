/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Smoke test — Phase 0. Confirms the library links, the public API
 * has the expected surface, and the stubs return the expected
 * status codes.
 *
 * Phase 1+: real tests land in property/ and integration/.
 */
#include <otlp-c/otlp.h>

#include <assert.h>
#include <stdio.h>

int
main(void)
{
	/* The library links and reports its version. */
	printf("otlp-c %s\n", otlp_version());
	printf("major=%d minor=%d patch=%d\n",
		OTLP_C_VERSION_MAJOR,
		OTLP_C_VERSION_MINOR,
		OTLP_C_VERSION_PATCH);
	assert(otlp_version() != NULL);

	/* Status code strings are populated. */
	for (int i = -100; i < 1; i++)
	{
		const char *s = otlp_strerror((otlp_status_t) i);

		if (s)
			printf("  status %d: %s\n", i, s);
	}

	/* Stubs return OTLP_ERR_NOT_IMPLEMENTED for the typical API
	 * surface. This is the expected Phase 0 behavior; the property
	 * tests in tests/property/ cover the same shape more rigorously.
	 */
	/* Span creation is real (Phase 4+). */
	otlp_span_t *span = otlp_span_create("hello");

	assert(span != NULL);
	otlp_span_free(span);

	/* Exporter create works (Phase 5+). */
	otlp_exporter_opts_t opts = { 0 };
	otlp_exporter_t *exp = otlp_exporter_create(&opts);

	assert(exp != NULL);
	otlp_exporter_free(exp);

	printf("[smoke] PASS — library loads and links, span + exporter "
	       "create/free work\n");
	return 0;
}
