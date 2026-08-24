# TODO 156 — Consumer projects out of CI YAML

**Status:** Complete (v0.6.10)
**Priority:** P2 (CI hygiene)

## The smell

Both consumer CI jobs carried their test fixtures as heredocs:
`cat > main.c << 'EOF'` and `cat > CMakeLists.txt << 'EOF'` inside
workflow YAML. That is code living in strings — no syntax
highlighting, no clang-format, no linting, unreviewable diffs,
duplicated across jobs, and CI is its first execution. PR #146's
first Windows run proved the cost: the embedded CMake had a bug
(native backslash SOURCE_DIR) that shipped straight to a red
check because nothing could exercise the code before GitHub did.

## What shipped

`tests/consumers/` — standalone consumer projects as real files
(each an independent CMake project, deliberately NOT wired into
`tests/CMakeLists.txt`):

- `find_package/` — installs then `find_package(otlp-c CONFIG)`;
  links and runs an emit→flush round trip (previously the CI
  version only printed `otlp_version()` — linked but barely used
  the API).
- `fetchcontent/` — embeds a checkout via FetchContent
  (`-DOTLP_C_SOURCE_DIR=<path>`); asserts the v0.6.9 hygiene
  invariants as first-class configure-time FATAL_ERRORs: the
  consumer's `CMAKE_INSTALL_LIBDIR` cache entry survives
  `MakeAvailable`, and no `CPackConfig.cmake` appears in its
  build tree. (The CPack check moved up from a bash `if ls` in
  the workflow into the consumer itself.)
- `README.md` — what each project covers + the exact local
  commands, identical to what CI runs.

The CI jobs are now three-line wrappers
(`cmake -S tests/consumers/... → build → ctest`); the stale
job-level comment about the Windows path-mangling workaround was
rewritten while there.

## Verification

- Both consumers pass locally with the README commands
  (find_package against a fresh install; fetchcontent against
  the checkout).
- **Mutation-tested both assertions**: bypassing the top-level
  guard on the LIBDIR pin → consumer fails "clobbered
  CMAKE_INSTALL_LIBDIR: lib"; bypassing it on CPack → consumer
  fails "CPack config leaked into the consumer build tree".
  Restored, both pass again.
- 49/49 tests; ci.yml YAML valid; no trailing whitespace;
  CMakeLists.txt untouched by the mutations (git-clean).
