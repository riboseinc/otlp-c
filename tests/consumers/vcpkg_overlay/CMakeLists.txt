cmake_minimum_required(VERSION 3.20)

project(consumer C)

find_package(otlp-c CONFIG REQUIRED)

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE otlp-c::otlp_c)

enable_testing()
add_test(NAME consumer_test COMMAND consumer)
