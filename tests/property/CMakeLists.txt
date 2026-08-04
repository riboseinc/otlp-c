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
