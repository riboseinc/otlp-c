# SPDX-License-Identifier: Apache-2.0
#
# Microbenchmarks. Not run by default CI; invoke manually with
# `cmake --build build --target otlp_bench_encode`.

add_executable(otlp_bench_encode bench_encode.c)
target_link_libraries(otlp_bench_encode PRIVATE otlp_c)
target_include_directories(otlp_bench_encode PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(otlp_bench_slab bench_slab.c)
target_link_libraries(otlp_bench_slab PRIVATE otlp_c)
target_include_directories(otlp_bench_slab PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(otlp_bench_emit bench_emit.c)
target_link_libraries(otlp_bench_emit PRIVATE otlp_c)
target_include_directories(otlp_bench_emit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(otlp_bench_encode_batch bench_encode_batch.c)
target_link_libraries(otlp_bench_encode_batch PRIVATE otlp_c)
target_include_directories(otlp_bench_encode_batch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(otlp_bench_logs bench_logs.c)
target_link_libraries(otlp_bench_logs PRIVATE otlp_c)
target_include_directories(otlp_bench_logs PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Benchmarks are opt-in to keep CI fast.
option(OTLP_C_BUILD_BENCH "Build the microbenchmarks" OFF)
if(NOT OTLP_C_BUILD_BENCH)
    set_target_properties(otlp_bench_encode PROPERTIES EXCLUDE_FROM_ALL TRUE)
    set_target_properties(otlp_bench_slab PROPERTIES EXCLUDE_FROM_ALL TRUE)
    set_target_properties(otlp_bench_emit PROPERTIES EXCLUDE_FROM_ALL TRUE)
    set_target_properties(otlp_bench_encode_batch PROPERTIES EXCLUDE_FROM_ALL TRUE)
    set_target_properties(otlp_bench_logs PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
