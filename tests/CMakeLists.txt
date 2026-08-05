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

# Phase 3 + Phase 5 echo tests use pthread (in-process TCP echo
# server). POSIX only; Windows skips via in-source #if defined(_WIN32)
# stubs but we don't compile the helper on Windows to avoid pulling
# in <arpa/inet.h>.
if(UNIX)
    add_executable(otlp_test_http_echo test_http_echo.c test_helper_echo.c)
    target_link_libraries(otlp_test_http_echo PRIVATE otlp_c Threads::Threads)
    add_test(NAME http-echo
        COMMAND otlp_test_http_echo)
    set_tests_properties(http-echo PROPERTIES LABELS "unit" TIMEOUT 30)

    add_executable(otlp_test_exporter_echo
        test_exporter_echo.c test_helper_echo.c)
    target_link_libraries(otlp_test_exporter_echo PRIVATE otlp_c Threads::Threads)
    add_test(NAME exporter-echo
        COMMAND otlp_test_exporter_echo)
    set_tests_properties(exporter-echo PROPERTIES LABELS "unit" TIMEOUT 60)
endif()

# Property tests — see property/ for the harness.
add_subdirectory(property)

# Integration tests — require docker for otelcol + Jaeger.
# Gated by OTLP_C_RUN_INTEGRATION=1 at runtime.
add_subdirectory(integration)
