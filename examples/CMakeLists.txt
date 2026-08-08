# SPDX-License-Identifier: Apache-2.0
#
# Example programs.

add_executable(otlp_example_minimal minimal.c)
target_link_libraries(otlp_example_minimal PRIVATE otlp_c)

# Multi-threaded demo: N worker threads emit into one exporter while a
# tick thread drains the queue. Uses pthread on POSIX, CreateThread on
# Windows. Demonstrates the library's core embedding pattern.
add_executable(otlp_example_multithread multithread.c)
target_link_libraries(otlp_example_multithread PRIVATE otlp_c)
if(UNIX AND NOT APPLE)
    target_link_libraries(otlp_example_multithread PRIVATE pthread)
endif()
