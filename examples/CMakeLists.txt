# SPDX-License-Identifier: Apache-2.0
#
# Example programs.

add_executable(otlp_example_minimal minimal.c)
target_link_libraries(otlp_example_minimal PRIVATE otlp_c)
