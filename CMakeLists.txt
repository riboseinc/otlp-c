cmake_minimum_required(VERSION 3.20)

project(otlp-c
    VERSION 0.1.0
    DESCRIPTION "Pure-C OTLP/HTTP client for OpenTelemetry"
    HOMEPAGE_URL "https://github.com/riboseinc/otlp-c"
    LANGUAGES C
)

set(OTLP_C_VERSION ${PROJECT_VERSION})
set(OTLP_C_VERSION_STRING "${PROJECT_VERSION}")

# Standards
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Position-independent code by default (so the static lib can be
# linked into a shared lib).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Hidden visibility by default; exported symbols opt in.
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Options
option(OTLP_C_BUILD_TESTS "Build unit + property + integration tests" OFF)
option(OTLP_C_BUILD_EXAMPLES "Build example programs" OFF)
option(OTLP_C_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(OTLP_C_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(BUILD_SHARED_LIBS "Build as shared library instead of static" OFF)

# Sanitizers
if(OTLP_C_ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()
if(OTLP_C_ENABLE_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()

# Warnings (gcc + clang)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wconversion -Wsign-conversion
        -Wundef -Wshadow -Wpointer-arith
        -Wformat=2 -Wno-format-nonliteral
        -Wwrite-strings -Wold-style-definition
        -Wmissing-prototypes
    )
endif()

# Threads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# Library target
add_library(otlp_c
    src/common.c
    src/span.c
    src/tracer.c
    src/exporter.c
    src/exporter_otel.c
    src/http_client.c
    src/protobuf_encode.c
    src/platform.c
)

add_library(otlp-c::otlp_c ALIAS otlp_c)

target_include_directories(otlp_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(otlp_c PRIVATE OTLP_C_BUILDING=1)

# For shared library consumers on Windows, define OTLP_C_SHARED_LIB
# so the import path is taken in visibility.h.
if(BUILD_SHARED_LIBS)
    target_compile_definitions(otlp_c PUBLIC OTLP_C_SHARED_LIB=1)
endif()

target_link_libraries(otlp_c PRIVATE Threads::Threads)

# Tests
if(OTLP_C_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(OTLP_C_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS otlp_c
    EXPORT otlp-c-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/otlp-c
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT otlp-c-targets
    FILE otlp-c-targets.cmake
    NAMESPACE otlp-c::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/otlp-c
)

configure_package_config_file(
    cmake/otlp-c-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/otlp-c-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/otlp-c
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/otlp-c-config-version.cmake
    VERSION ${OTLP_C_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/otlp-c-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/otlp-c-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/otlp-c
)

# Status
message(STATUS "otlp-c ${OTLP_C_VERSION_STRING}")
message(STATUS "  Build tests:    ${OTLP_C_BUILD_TESTS}")
message(STATUS "  Build examples: ${OTLP_C_BUILD_EXAMPLES}")
message(STATUS "  Shared library: ${BUILD_SHARED_LIBS}")
message(STATUS "  ASan:           ${OTLP_C_ENABLE_ASAN}")
message(STATUS "  UBSan:          ${OTLP_C_ENABLE_UBSAN}")
