#!/usr/bin/env python3
"""Docs/site sync gate: the hand-maintained truth surfaces vs the
machine-knowable facts.

Checked by CI (conformance-gates job) so drift is a red check,
not a human discovery. Covers:

  1. Version quartet: version.h == CMakeLists == vcpkg.json ==
     ports/otlp-c/vcpkg.json
  2. Changelog coherence: CHANGELOG.md's newest entry and the
     site changelog page's newest entry both == the quartet
     version
  3. Env-var parity: every OTEL_* name read by src/env_config.c
     appears in the site's EnvVarExplorer island

Exit 0 = in sync. Exit 1 = printed mismatches.
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
fail = []


def check(ok, msg):
    if not ok:
        fail.append(msg)


# 1. The version quartet.
vh = (ROOT / "include/otlp-c/version.h").read_text()
major = re.search(r"#define OTLP_C_VERSION_MAJOR (\d+)", vh).group(1)
minor = re.search(r"#define OTLP_C_VERSION_MINOR (\d+)", vh).group(1)
patch = re.search(r"#define OTLP_C_VERSION_PATCH (\d+)", vh).group(1)
version = f"{major}.{minor}.{patch}"

cml = (ROOT / "CMakeLists.txt").read_text()
check(f"VERSION {version}" in cml, f"CMakeLists.txt VERSION is not {version}")

for mf in ("vcpkg.json", "ports/otlp-c/vcpkg.json"):
    manifest = json.loads((ROOT / mf).read_text())
    check(
        manifest.get("version") == version,
        f"{mf} version is {manifest.get('version')}, not {version}",
    )

# 2. Changelog coherence (newest entries, newest-first files).
ch = (ROOT / "CHANGELOG.md").read_text()
m = re.search(r"^## \[(\d+\.\d+\.\d+)\]", ch, re.M)
check(
    m and m.group(1) == version,
    f"CHANGELOG.md newest entry is {m.group(1) if m else 'none'}, not {version}",
)

astro = (ROOT / "website/src/pages/docs/changelog.astro").read_text()
m = re.search(r'v:\s*"(\d+\.\d+\.\d+)"', astro)
check(
    m and m.group(1) == version,
    "website changelog newest entry is "
    f"{m.group(1) if m else 'none'}, not {version}",
)

# 2b. The install examples' GIT_TAG pins ride every release.
for doc in ("README.md", "docs/quickstart.md"):
    text = (ROOT / doc).read_text()
    tags = re.findall(r"GIT_TAG\s+v(\d+\.\d+\.\d+)", text)
    check(
        bool(tags) and all(t == version for t in tags),
        f"{doc} GIT_TAG examples are {tags}, not the release version {version}",
    )

# 3. Env-var parity: env_config.c's getenv table vs the site island.
env_src = (ROOT / "src/env_config.c").read_text()
code_vars = set(re.findall(r'getenv\("(OTEL_[A-Z_]+)"\)', env_src))

island = (ROOT / "website/src/components/EnvVarExplorer.vue").read_text()
site_vars = set(re.findall(r'name:\s*"(OTEL_[A-Z_]+)"', island))

check(
    not (code_vars - site_vars),
    f"env vars in env_config.c but not on the site: {sorted(code_vars - site_vars)}",
)
check(
    not (site_vars - code_vars),
    f"env vars on the site but not read by env_config.c: {sorted(site_vars - code_vars)}",
)

if fail:
    print("docs-sync: FAILED")
    for f in fail:
        print(f"  - {f}")
    sys.exit(1)
print(
    f"docs-sync: OK — quartet {version}, changelogs coherent, "
    f"{len(code_vars)} env vars in parity"
)
