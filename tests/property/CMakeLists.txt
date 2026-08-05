# SPDX-License-Identifier: Apache-2.0
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
