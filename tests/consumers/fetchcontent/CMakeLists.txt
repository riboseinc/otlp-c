cmake_minimum_required(VERSION 3.20)

project(consumer C)

# Points at the otlp-c checkout to embed. CI passes the workspace;
# locally: -DOTLP_C_SOURCE_DIR=/path/to/otlp-c
if(NOT DEFINED OTLP_C_SOURCE_DIR)
    message(FATAL_ERROR "Set -DOTLP_C_SOURCE_DIR=<otlp-c checkout>")
endif()
# Native backslash paths (Windows) break FetchContent's subbuild
# CMakeLists as invalid escapes; normalize first.
file(TO_CMAKE_PATH "${OTLP_C_SOURCE_DIR}" OTLP_C_SOURCE_DIR)

# The consumer picks its install layout BEFORE embedding otlp-c;
# the value must survive (an embedded otlp-c must be side-effect
# free — the CACHE FORCE pin used to clobber it, v0.6.9).
set(CMAKE_INSTALL_LIBDIR "lib/consumer-check" CACHE PATH "")

include(FetchContent)
FetchContent_Declare(otlp-c SOURCE_DIR "${OTLP_C_SOURCE_DIR}")
FetchContent_MakeAvailable(otlp-c)

if(NOT "${CMAKE_INSTALL_LIBDIR}" STREQUAL "lib/consumer-check")
    message(FATAL_ERROR
        "otlp-c clobbered CMAKE_INSTALL_LIBDIR: ${CMAKE_INSTALL_LIBDIR}")
endif()
if(EXISTS "${CMAKE_BINARY_DIR}/CPackConfig.cmake")
    message(FATAL_ERROR
        "CPack config leaked into the consumer build tree")
endif()

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE otlp-c::otlp_c)

enable_testing()
add_test(NAME consumer_test COMMAND consumer)
