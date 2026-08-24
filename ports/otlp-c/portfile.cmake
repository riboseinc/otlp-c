# SPDX-License-Identifier: BSD-3-Clause
#
# vcpkg port recipe for otlp-c, building the LOCAL checkout:
#
#   vcpkg install --x-manifest-root=<consumer dir> \
#       --overlay-ports=<this repo>/ports
#
# The source is the repo containing this ports/ directory — no
# REF/SHA to keep current, so the recipe can never drift behind
# the releases (it was pinned at v0.5.17 with a placeholder SHA
# for a hundred releases before v0.6.15, and nothing ever built
# it). CI exercises this exact path (the "vcpkg overlay consumer"
# job).
#
# For upstream submission to microsoft/vcpkg, replace the local
# SOURCE_PATH with vcpkg_from_github(OUT_SOURCE_PATH SOURCE_PATH
# REPO riboseinc/otlp-c REF <tag> SHA512 <hash> ...).

set(SOURCE_PATH "${CURRENT_PORT_DIR}/../..")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DOTLP_C_BUILD_TESTS=OFF
        -DOTLP_C_BUILD_EXAMPLES=OFF
        -DOTLP_C_BUILD_BENCH=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/otlp-c)

file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/debug/include"
    "${CURRENT_PACKAGES_DIR}/debug/share"
)

file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
