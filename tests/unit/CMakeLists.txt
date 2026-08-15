# SPDX-License-Identifier: Apache-2.0
#
# Targeted unit tests for individual module functions. Complements
# the property tests with specific known-answer assertions.

add_executable(otlp_unit_protobuf test_unit_protobuf.c)
target_link_libraries(otlp_unit_protobuf PRIVATE otlp_c)
target_include_directories(otlp_unit_protobuf PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
add_test(NAME unit-protobuf
    COMMAND otlp_unit_protobuf)
set_tests_properties(unit-protobuf PROPERTIES LABELS "unit")

add_executable(otlp_unit_span test_unit_span.c)
target_link_libraries(otlp_unit_span PRIVATE otlp_c)
add_test(NAME unit-span
    COMMAND otlp_unit_span)
set_tests_properties(unit-span PROPERTIES LABELS "unit")

add_executable(otlp_unit_metric test_unit_metric.c)
target_link_libraries(otlp_unit_metric PRIVATE otlp_c)
target_include_directories(otlp_unit_metric PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
add_test(NAME unit-metric
    COMMAND otlp_unit_metric)
set_tests_properties(unit-metric PROPERTIES LABELS "unit")

add_executable(otlp_unit_log test_unit_log.c)
target_link_libraries(otlp_unit_log PRIVATE otlp_c)
target_include_directories(otlp_unit_log PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
add_test(NAME unit-log
    COMMAND otlp_unit_log)
set_tests_properties(unit-log PROPERTIES LABELS "unit")
