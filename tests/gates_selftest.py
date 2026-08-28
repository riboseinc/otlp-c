#!/usr/bin/env python3
"""Gates self-test: every gate check branch is proven to FAIL.

A check that silently passes nothing is worse than no check —
the v0.5.99 lesson (assert helpers that evaluated but didn't
enforce), applied to the gate suite itself. This script applies
one crafted lie per check branch, asserts the gate exits
nonzero AND names the mutated file, then git-restores it.

Safety: aborts without touching anything if the tree is dirty.
CI runs it after the gates pass, in an ephemeral checkout.
"""
import subprocess
import sys

GATES = ["tests/site_docs_sync.py", "tests/include_lint.py"]

# (file, find, replace, gate, expected-in-output)
MUTATIONS = [
    ("include/otlp-c/version.h",
     "OTLP_C_VERSION_PATCH ", "OTLP_C_VERSION_PATCH 987654 ",
     GATES[0], "CMakeLists.txt"),
    ("CHANGELOG.md",
     "## [", "## [0.0.0-fake] and [", GATES[0], "CHANGELOG.md"),
    ("website/src/pages/docs/changelog.astro",
     'v: "', 'v: "0.0.0",\n    unused: "', GATES[0], "changelog"),
    ("website/src/components/EnvVarExplorer.vue",
     'name: "OTEL_SERVICE_NAME"', 'name: "OTEL_SERVICE_NAME_GONE"',
     GATES[0], "env_config.c"),
    ("website/src/components/EnvVarExplorer.vue",
     '{', '{ name: "OTEL_EXPORTER_OTLP_FAKE", desc: "x" },\n    {',
     GATES[0], "env_config.c"),
    ("README.md",
     "GIT_TAG        v", "GIT_TAG        v0.0.0 and v",
     GATES[0], "README.md"),
    ("docs/cookbook.md",
     "## ", "## otlp_totally_fake_symbol()\n\n## ",
     GATES[0], "cookbook"),
    ("src/common.c",
     '#include ', '#include "span_internal.h"\n#include ',
     GATES[1], "common.c"),
    ("docs/architecture.md",
     "| `common.c` |", "| `comm0n.c` |",
     GATES[1], "common.c"),
]


def sh(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def read(path):
    with open(path) as f:
        return f.read()


def write(path, text):
    with open(path, "w") as f:
        f.write(text)


def main():
    # Untracked files are fine — only tracked files get mutated
    # and restored; a MODIFIED tracked file could be user work.
    dirty = [l for l in sh("git", "status", "--porcelain").stdout
             .splitlines() if not l.startswith("??")]
    if dirty:
        print("gates-selftest: ABORT — working tree dirty; "
              "refusing to mutate and restore")
        return 1

    failed = 0
    for path, find, replace, gate, expect in MUTATIONS:
        original = read(path)
        assert find in original, f"mutation anchor missing in {path}: {find!r}"
        write(path, original.replace(find, replace, 1))

        r = sh("python3", gate)
        ok = r.returncode != 0 and expect in (r.stdout + r.stderr)

        sh("git", "checkout", "--", path)
        restored = read(path) == original

        status = "caught" if ok and restored else "MISSED"
        if status != "caught":
            failed += 1
        print(f"  {status:7} {gate.split('/')[-1]:20} {path}"
              f"{' (restore failed!)' if not restored else ''}")
        if not ok and r.returncode == 0:
            print(f"          gate PASSED on a lie: {find!r} in {path}")

    for gate in GATES:
        if sh("python3", gate).returncode != 0:
            print(f"gates-selftest: ABORT — {gate} fails on the clean tree")
            return 1

    if failed:
        print(f"gates-selftest: FAILED — {failed} mutation(s) not caught")
        return 1
    print(f"gates-selftest: OK — {len(MUTATIONS)} lies caught, "
          "tree restored, clean-tree gates green")
    return 0


if __name__ == "__main__":
    sys.exit(main())
