/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Property test harness — Phase 0 seed. The implementing agent
 * will populate this file with real properties as each phase of
 * the roadmap lands:
 *
 *   Phase 1: P-VARINT-ROUNDTRIP, P-VARINT-SIZE
 *   Phase 2: P-ENCODE-NEVER-CORRUPT, P-ENCODE-EMPTY
 *   Phase 3: P-HTTP-NEVER-HANG, P-HTTP-NO-CRASH
 *   Phase 4: P-SPAN-ATTRIBUTES, P-SPAN-IDS-UNIQUE
 *   Phase 5: P-EXPORT-NEVER-CORRUPT, P-EXPORT-NO-LEAK
 *
 * The harness (xorshift PRNG + iteration loop) lives in this
 * directory's prng.h and property_harness.h, mirroring retrace's
 * test/property/ layout.
 */
#include "prng.h"
#include "property_harness.h"

#include <otlp-c/otlp.h>

/* Phase 0 seed property: the library's version string is non-NULL
 * and matches the OTLP_C_VERSION_STRING macro. Real properties
 * land in Phase 1+. */
static int
prop_version_consistent(uint64_t seed)
{
	const char *v = otlp_version();

	(void) seed;
	return (v != NULL && v[0] != '\0' && str_eq(v, OTLP_C_VERSION_STRING))
		? 1
		: 0;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(
		prop_version_consistent, "prop_version_consistent", 100, 1);

	if (failures)
		printf("[property] %d property(ies) failed\n", failures);
	else
		printf("[property] all properties passed\n");

	return failures ? 1 : 0;
}
