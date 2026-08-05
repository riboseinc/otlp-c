# SPDX-License-Identifier: Apache-2.0
#
# Integration test. Requires Docker (docker compose) to run otelcol
# + Jaeger. Gated by OTLP_C_RUN_INTEGRATION=1 so the test no-ops
# cleanly when Docker is not installed.

if(UNIX)
	add_executable(otlp_integration_jaeger test_integration_jaeger.c)
	target_link_libraries(otlp_integration_jaeger PRIVATE otlp_c)
	target_include_directories(otlp_integration_jaeger PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../)

	add_test(NAME integration-jaeger
		COMMAND otlp_integration_jaeger)
	set_tests_properties(integration-jaeger PROPERTIES
		LABELS "integration"
		TIMEOUT 60)
endif()
