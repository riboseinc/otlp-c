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
import glob
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

# 4. API-mention parity on reader-facing surfaces: every otlp_*
headers = "".join(
    (ROOT / f).read_text() for f in glob.glob("include/otlp-c/*.h")
)
syms = set(re.findall(r"\b(otlp_[a-z0-9_]+)\s*\(", headers))
types = set(re.findall(r"\b(otlp_[a-z0-9_]+_t)\b", headers))

# (section 4 continues) every otlp_*
#    mention must exist in the public headers or the allowlist.
#    Internal/history docs (roadmap, spec, architecture, CLAUDE.md)
#    document internals by design and are out of scope.
reader_facing = sorted(
    set(glob.glob("website/src/**/*.astro", recursive=True))
    | set(glob.glob("website/src/**/*.vue", recursive=True))
    | {"docs/quickstart.md", "docs/cookbook.md", "README.md"}
)
allow = {
    "otlp_span", "otlp_metric", "otlp_exporter",  # prose short names
    "otlp_c", "otlp_add_property_test",           # target; build helper
    "otlp_malloc",  # slab.h's own docstring vocabulary (routing prose)
}
known = syms | types | allow
known |= {f"otlp_bench_{n}" for n in (
    "emit", "encode", "encode_batch", "logs", "slab")}
for rf in reader_facing:
    text = (ROOT / rf).read_text()
    unknown = sorted(
        m for m in set(re.findall(r"\botlp_[a-z0-9_]+\b", text))
        if m not in known and not m.startswith("otlp-c")
    )
    check(
        not unknown,
        f"{rf} mentions API that does not exist: {unknown}",
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
