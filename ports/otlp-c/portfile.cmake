# SPDX-License-Identifier: Apache-2.0
#
# vcpkg port recipe for otlp-c. Consumers can use this as an overlay:
#   vcpkg install otlp-c --overlay-ports=ports
#
# For upstream submission to microsoft/vcpkg, copy this directory to
# ports/otlp-c/ in the vcpkg repo and adjust the SHA/REF below.

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO riboseinc/otlp-c
    REF v0.5.17
    SHA512 00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DOTLP_C_BUILD_TESTS=OFF
        -DOTLP_C_BUILD_EXAMPLES=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/otlp-c)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
