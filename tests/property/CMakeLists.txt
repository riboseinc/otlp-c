# SPDX-License-Identifier: BSD-3-Clause
#
# Property-based tests. QuickCheck-style.
#
# One registration helper owns the boilerplate so adding a property
# is a one-liner. Labels encode the soak protocol:
#   soak   — CPU-side; scales with OTLP_C_PROPERTY_ITERS; TIMEOUT
#            600 s so the weekly 100k soak can run through ctest
#   timing — sleeps real wall-clock per iteration; default 1000
#            IS the soak (do not scale); TIMEOUT 60
#
# See .github/workflows/soak.yml and docs/roadmap.md (v1.0.4).

# otlp_add_property_test(<name>
#   SOURCES <src>...
#   [THREADS]
#   [TIMING]          # wall-clock; else soak-capable
#   [RUN_SERIAL]
#   [INCLUDES <dir>...])
function(otlp_add_property_test name)
	cmake_parse_arguments(ARG
		"THREADS;TIMING;RUN_SERIAL"
		""
		"SOURCES;INCLUDES"
		${ARGN})
	if(NOT ARG_SOURCES)
		message(FATAL_ERROR "otlp_add_property_test(${name}): SOURCES required")
	endif()

	set(_exe "otlp_property_${name}")
	# Test names use hyphens (property-attribute-roundtrip).
	string(REPLACE "_" "-" _test "property-${name}")

	add_executable(${_exe} ${ARG_SOURCES})
	if(ARG_THREADS)
		target_link_libraries(${_exe} PRIVATE otlp_c Threads::Threads)
	else()
		target_link_libraries(${_exe} PRIVATE otlp_c)
	endif()
	target_include_directories(${_exe} PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		${ARG_INCLUDES})

	add_test(NAME ${_test} COMMAND ${_exe})

	# LABELS must be one quoted "a;b;c" argument — unquoted
	# expansion turns the semicolons into separate PROPERTIES keys.
	if(ARG_TIMING)
		set(_labels "property;unit;timing")
		set(_timeout 60)
	else()
		set(_labels "property;unit;soak")
		set(_timeout 600)
	endif()

	if(ARG_RUN_SERIAL)
		set_tests_properties(${_test} PROPERTIES
			LABELS "${_labels}"
			TIMEOUT ${_timeout}
			RUN_SERIAL ON)
	else()
		set_tests_properties(${_test} PROPERTIES
			LABELS "${_labels}"
			TIMEOUT ${_timeout})
	endif()
endfunction()

otlp_add_property_test(seed SOURCES test_property_seed.c)
otlp_add_property_test(varint SOURCES test_property_varint.c)
otlp_add_property_test(encoder SOURCES test_property_encoder.c)
otlp_add_property_test(messages SOURCES test_property_messages.c)
otlp_add_property_test(url_parse SOURCES test_property_url_parse.c)
otlp_add_property_test(span SOURCES test_property_span.c)
otlp_add_property_test(attribute_roundtrip
	SOURCES test_property_attribute_roundtrip.c)

if(UNIX)
	otlp_add_property_test(mpsc
		SOURCES test_property_mpsc.c
		THREADS)
endif()

otlp_add_property_test(w3c SOURCES test_property_w3c.c)

if(NOT WIN32)
	otlp_add_property_test(fuzz
		SOURCES test_property_fuzz.c ../test_helper_echo.c
		THREADS
		INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/..)
else()
	otlp_add_property_test(fuzz SOURCES test_property_fuzz.c)
endif()

otlp_add_property_test(exporter SOURCES test_property_exporter.c)
otlp_add_property_test(metrics SOURCES test_property_metrics.c)
otlp_add_property_test(logs SOURCES test_property_logs.c)
otlp_add_property_test(events_context
	SOURCES test_property_events_context.c)
otlp_add_property_test(sampler SOURCES test_property_sampler.c)
otlp_add_property_test(slab SOURCES test_property_slab.c)

if(UNIX)
	otlp_add_property_test(keepalive
		SOURCES test_property_keepalive.c ../test_helper_echo.c
		THREADS TIMING RUN_SERIAL
		INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()

otlp_add_property_test(flush SOURCES test_property_flush.c)
otlp_add_property_test(resource_attrs
	SOURCES test_property_resource_attrs.c)
otlp_add_property_test(flush_timeout
	SOURCES test_property_flush_timeout.c
	TIMING)
otlp_add_property_test(baggage SOURCES test_property_baggage.c)
otlp_add_property_test(diagnostics SOURCES test_property_diagnostics.c)
otlp_add_property_test(http_timeout
	SOURCES test_property_http_timeout.c
	TIMING)
otlp_add_property_test(async_metrics
	SOURCES test_property_async_metrics.c
	TIMING)
otlp_add_property_test(retry SOURCES test_property_retry.c)
