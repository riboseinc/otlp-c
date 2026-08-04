# SPDX-License-Identifier: Apache-2.0
#
# Test suite: unit + property + integration.

enable_testing()

# Unit tests — per-module function tests.
# (Phase 1+ adds these; Phase 0 ships a placeholder.)

add_executable(otlp_test_smoke test_smoke.c)
target_link_libraries(otlp_test_smoke PRIVATE otlp_c)
add_test(NAME smoke
	COMMAND otlp_test_smoke)
set_tests_properties(smoke PROPERTIES LABELS "smoke;unit")

# Property tests — see property/ for the harness.
add_subdirectory(property)

# Integration tests — require docker for otelcol.
# add_subdirectory(integration)
