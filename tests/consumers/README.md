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
