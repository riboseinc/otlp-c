# SPDX-License-Identifier: BSD-3-Clause
#
# Property-based tests. QuickCheck-style. See docs/roadmap.md
# Phase 1+ for the planned properties.

add_executable(otlp_property_seed test_property_seed.c)
target_link_libraries(otlp_property_seed PRIVATE otlp_c)
target_include_directories(otlp_property_seed PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-seed
	COMMAND otlp_property_seed)
set_tests_properties(property-seed PROPERTIES
	LABELS "property;unit"
	TIMEOUT 60)

# Phase 1: protobuf wire encoder.
add_executable(otlp_property_varint test_property_varint.c)
target_link_libraries(otlp_property_varint PRIVATE otlp_c)
target_include_directories(otlp_property_varint PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-varint
	COMMAND otlp_property_varint)
set_tests_properties(property-varint PROPERTIES
	LABELS "property;unit"
	TIMEOUT 60)

add_executable(otlp_property_encoder test_property_encoder.c)
target_link_libraries(otlp_property_encoder PRIVATE otlp_c)
target_include_directories(otlp_property_encoder PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-encoder
	COMMAND otlp_property_encoder)
set_tests_properties(property-encoder PROPERTIES
	LABELS "property;unit"
	TIMEOUT 60)

# Phase 2: OTLP message encoders.
add_executable(otlp_property_messages test_property_messages.c)
target_link_libraries(otlp_property_messages PRIVATE otlp_c)
target_include_directories(otlp_property_messages PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-messages
	COMMAND otlp_property_messages)
set_tests_properties(property-messages PROPERTIES
	LABELS "property;unit"
	TIMEOUT 60)

# Phase 3: HTTP URL parser.
add_executable(otlp_property_url_parse test_property_url_parse.c)
target_link_libraries(otlp_property_url_parse PRIVATE otlp_c)
target_include_directories(otlp_property_url_parse PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-url-parse
	COMMAND otlp_property_url_parse)
set_tests_properties(property-url-parse PROPERTIES
	LABELS "property;unit"
	TIMEOUT 60)

# Phase 4: span + tracer.
add_executable(otlp_property_span test_property_span.c)
target_link_libraries(otlp_property_span PRIVATE otlp_c)
target_include_directories(otlp_property_span PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-span
	COMMAND otlp_property_span)
set_tests_properties(property-span PROPERTIES
	LABELS "property;unit"
	TIMEOUT 60)

add_executable(otlp_property_attribute_roundtrip
    test_property_attribute_roundtrip.c)
target_link_libraries(otlp_property_attribute_roundtrip PRIVATE otlp_c)
target_include_directories(otlp_property_attribute_roundtrip PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-attribute-roundtrip
	COMMAND otlp_property_attribute_roundtrip)
set_tests_properties(property-attribute-roundtrip PROPERTIES
	LABELS "property;unit"
	TIMEOUT 60)

# Phase 12: MPSC queue contention (POSIX only — uses pthreads).
if(UNIX)
    add_executable(otlp_property_mpsc test_property_mpsc.c)
    target_link_libraries(otlp_property_mpsc PRIVATE otlp_c Threads::Threads)
    target_include_directories(otlp_property_mpsc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME property-mpsc
        COMMAND otlp_property_mpsc)
    set_tests_properties(property-mpsc PROPERTIES
        LABELS "property;unit"
        TIMEOUT 120)
endif()

# v0.4: W3C traceparent format/parse.
add_executable(otlp_property_w3c test_property_w3c.c)
target_link_libraries(otlp_property_w3c PRIVATE otlp_c)
target_include_directories(otlp_property_w3c PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-w3c
	COMMAND otlp_property_w3c)
set_tests_properties(property-w3c PROPERTIES
	LABELS "property;unit"
	TIMEOUT 60)

# Fuzz-like property tests: arbitrary byte sequences → parsers.
# Checks for crashes only (no semantic invariant).
if(NOT WIN32)
    add_executable(otlp_property_fuzz test_property_fuzz.c ../../tests/test_helper_echo.c)
    # The echo helper calls pthread_create: link Threads everywhere
    # it is compiled in (FreeBSD keeps pthreads out of libc).
    target_link_libraries(otlp_property_fuzz PRIVATE otlp_c Threads::Threads)
else()
    add_executable(otlp_property_fuzz test_property_fuzz.c)
    target_link_libraries(otlp_property_fuzz PRIVATE otlp_c)
endif()
target_include_directories(otlp_property_fuzz PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_test(NAME property-fuzz
	COMMAND otlp_property_fuzz)
set_tests_properties(property-fuzz PROPERTIES
	LABELS "property;unit"
	TIMEOUT 60)

# v0.5: exporter batching property tests. Uses null_transport mode —
# no echo server, no threads, no timing sensitivity. Fully deterministic
# across all platforms.
add_executable(otlp_property_exporter test_property_exporter.c)
target_link_libraries(otlp_property_exporter PRIVATE otlp_c)
target_include_directories(otlp_property_exporter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-exporter
    COMMAND otlp_property_exporter)
set_tests_properties(property-exporter PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# v0.4: OTLP metrics encoder.
add_executable(otlp_property_metrics test_property_metrics.c)
target_link_libraries(otlp_property_metrics PRIVATE otlp_c)
target_include_directories(otlp_property_metrics PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-metrics
    COMMAND otlp_property_metrics)
set_tests_properties(property-metrics PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# v0.4: OTLP logs encoder.
add_executable(otlp_property_logs test_property_logs.c)
target_link_libraries(otlp_property_logs PRIVATE otlp_c)
target_include_directories(otlp_property_logs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-logs
    COMMAND otlp_property_logs)
set_tests_properties(property-logs PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# v0.5: span events / links / trace_state + SpanContext propagation.
add_executable(otlp_property_events_context test_property_events_context.c)
target_link_libraries(otlp_property_events_context PRIVATE otlp_c)
target_include_directories(otlp_property_events_context PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-events-context
    COMMAND otlp_property_events_context)
set_tests_properties(property-events-context PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# v0.5: sampler interface.
add_executable(otlp_property_sampler test_property_sampler.c)
target_link_libraries(otlp_property_sampler PRIVATE otlp_c)
target_include_directories(otlp_property_sampler PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-sampler
    COMMAND otlp_property_sampler)
set_tests_properties(property-sampler PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# v0.5: slab allocator.
add_executable(otlp_property_slab test_property_slab.c)
target_link_libraries(otlp_property_slab PRIVATE otlp_c)
target_include_directories(otlp_property_slab PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-slab
    COMMAND otlp_property_slab)
set_tests_properties(property-slab PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# v0.5: HTTP keep-alive pooling (POSIX only — uses echo server).
if(UNIX)
    add_executable(otlp_property_keepalive test_property_keepalive.c
        ../test_helper_echo.c)
    target_link_libraries(otlp_property_keepalive PRIVATE otlp_c Threads::Threads)
    target_include_directories(otlp_property_keepalive PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)

    add_test(NAME property-keepalive
        COMMAND otlp_property_keepalive)
    set_tests_properties(property-keepalive PROPERTIES
        LABELS "property;unit"
        TIMEOUT 60
        RUN_SERIAL ON)
endif()

# v0.5: metric/log flush property tests.
add_executable(otlp_property_flush test_property_flush.c)
target_link_libraries(otlp_property_flush PRIVATE otlp_c)
target_include_directories(otlp_property_flush PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-flush
    COMMAND otlp_property_flush)
set_tests_properties(property-flush PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# v0.5.20: Resource attributes (additional Resource KeyValues).
add_executable(otlp_property_resource_attrs test_property_resource_attrs.c)
target_link_libraries(otlp_property_resource_attrs PRIVATE otlp_c)
target_include_directories(otlp_property_resource_attrs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-resource-attrs
    COMMAND otlp_property_resource_attrs)
set_tests_properties(property-resource-attrs PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# v0.5.21: configurable flush_timeout_ms.
add_executable(otlp_property_flush_timeout test_property_flush_timeout.c)
target_link_libraries(otlp_property_flush_timeout PRIVATE otlp_c)
target_include_directories(otlp_property_flush_timeout PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-flush-timeout
    COMMAND otlp_property_flush_timeout)
set_tests_properties(property-flush-timeout PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# v0.5.22: W3C Baggage + traceparent_format_raw DRY.
add_executable(otlp_property_baggage test_property_baggage.c)
target_link_libraries(otlp_property_baggage PRIVATE otlp_c)
target_include_directories(otlp_property_baggage PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-baggage
    COMMAND otlp_property_baggage)
set_tests_properties(property-baggage PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# v0.5.23: diagnostic callback (otlp_exporter_set_logger).
add_executable(otlp_property_diagnostics test_property_diagnostics.c)
target_link_libraries(otlp_property_diagnostics PRIVATE otlp_c)
target_include_directories(otlp_property_diagnostics PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-diagnostics
    COMMAND otlp_property_diagnostics)
set_tests_properties(property-diagnostics PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# v0.5.25: HTTP connect timeout enforcement.
add_executable(otlp_property_http_timeout test_property_http_timeout.c)
target_link_libraries(otlp_property_http_timeout PRIVATE otlp_c)
target_include_directories(otlp_property_http_timeout PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-http-timeout
    COMMAND otlp_property_http_timeout)
set_tests_properties(property-http-timeout PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# v0.5.28: async metric/log batching.
add_executable(otlp_property_async_metrics test_property_async_metrics.c)
target_link_libraries(otlp_property_async_metrics PRIVATE otlp_c)
target_include_directories(otlp_property_async_metrics PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME property-async-metrics
    COMMAND otlp_property_async_metrics)
set_tests_properties(property-async-metrics PROPERTIES
    LABELS "property;unit"
    TIMEOUT 60)

# Retry timing policy properties (v0.6.13): full-jitter bounds,
# shift-count cap, Retry-After floor semantics — pure functions,
# portable.
add_executable(otlp_property_retry test_property_retry.c)
target_link_libraries(otlp_property_retry PRIVATE otlp_c)
add_test(NAME property-retry
	COMMAND otlp_property_retry)
set_tests_properties(property-retry PROPERTIES LABELS "property;unit")
