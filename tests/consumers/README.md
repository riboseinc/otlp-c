# Consumer projects

Standalone CMake projects that consume otlp-c the way a third
party would. Driven by CI (`.github/workflows/ci.yml`, jobs
"CMake find_package consumer" / "CMake FetchContent consumer")
and runnable locally with the exact same commands.

They are NOT part of the otlp-c build tree — no
`add_subdirectory(consumers)` anywhere. Each directory is an
independent project with its own `cmake_minimum_required` /
`project()`, deliberately outside `tests/CMakeLists.txt`.

| Directory | Consumption mode | Asserts |
|---|---|---|
| `find_package/` | `cmake --install` + `find_package(otlp-c CONFIG)` | links, runs an emit→flush round trip |
| `fetchcontent/` | `FetchContent` / `add_subdirectory` embedding | round trip + no side effects on the parent (install-layout cache entry survives, no CPack files in the consumer's build tree) |
| `vcpkg_overlay/` | vcpkg manifest install via the in-repo overlay port (`ports/`) | installs through `vcpkg_cmake_config_fixup`, links, round trip |

## Run locally (mirrors CI)

```sh
# find_package: install first
cmake -B build && cmake --build build && cmake --install build --prefix /tmp/otlp-c-prefix
cmake -S tests/consumers/find_package -B /tmp/fp-build \
    -DCMAKE_PREFIX_PATH=/tmp/otlp-c-prefix
cmake --build /tmp/fp-build && ctest --test-dir /tmp/fp-build

# fetchcontent: no install needed
cmake -S tests/consumers/fetchcontent -B /tmp/fc-build \
    -DOTLP_C_SOURCE_DIR=$PWD
cmake --build /tmp/fc-build && ctest --test-dir /tmp/fc-build
```

The `fetchcontent/` project mirrors the quickstart's FetchContent
instructions (`docs/quickstart.md`), with `SOURCE_DIR` instead of
`GIT_REPOSITORY` so it runs hermetically against a checkout.

## vcpkg overlay

Requires a vcpkg checkout (any recent one):

```sh
git clone --depth 1 https://github.com/microsoft/vcpkg.git /tmp/vcpkg
/tmp/vcpkg/bootstrap-vcpkg.sh
cmake -S tests/consumers/vcpkg_overlay -B /tmp/ovc-build \
    -DCMAKE_TOOLCHAIN_FILE=/tmp/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_OVERLAY_PORTS=$PWD/ports
cmake --build /tmp/ovc-build && ctest --test-dir /tmp/ovc-build
```
