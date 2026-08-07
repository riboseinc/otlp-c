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

    # Concurrency stress test: 8 threads x 200 spans into one exporter.
    # Catches data races in the MPSC queue + tracer PRNG under ASAN.
    add_executable(otlp_test_concurrency_stress
        test_concurrency_stress.c test_helper_echo.c)
    target_link_libraries(otlp_test_concurrency_stress PRIVATE otlp_c Threads::Threads)
    add_test(NAME concurrency-stress
        COMMAND otlp_test_concurrency_stress)
    set_tests_properties(concurrency-stress PROPERTIES LABELS "unit;stress" TIMEOUT 60)

    # Exporter retry test: uses null_transport mode — no echo server,
    # no threads, no timing sensitivity. Runs on all platforms.
    add_executable(otlp_test_exporter_retry test_exporter_retry.c)
    target_link_libraries(otlp_test_exporter_retry PRIVATE otlp_c)
    add_test(NAME exporter-retry
        COMMAND otlp_test_exporter_retry)
    set_tests_properties(exporter-retry PROPERTIES
        LABELS "unit" TIMEOUT 30)
endif()

# Custom allocator hook test.
add_executable(otlp_test_allocator test_allocator.c)
target_link_libraries(otlp_test_allocator PRIVATE otlp_c)
add_test(NAME allocator
    COMMAND otlp_test_allocator)
set_tests_properties(allocator PROPERTIES LABELS "unit")

add_subdirectory(unit)

# Property tests — see property/ for the harness.
add_subdirectory(property)

# Integration tests — require docker for otelcol + Jaeger.
# Gated by OTLP_C_RUN_INTEGRATION=1 at runtime.
add_subdirectory(integration)
