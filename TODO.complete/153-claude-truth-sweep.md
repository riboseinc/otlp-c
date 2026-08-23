# TODO 153 — CLAUDE.md truth sweep

**Status:** Complete (v0.6.7)
**Priority:** P3 (docs-truth; CLAUDE.md is the agent's entry point)

## Finding

The CI-claims audit (triggered by the thin workflows directory)
found CLAUDE.md asserting enforcement that doesn't exist:

- `.github/workflows/checkpatch.yml` + `ci/checkpatch.sh` —
  deleted in the "jemalloc-style release + checks workflow"
  consolidation (f2117d4); both references survived.
- `.github/workflows/build.yml` — renamed to `ci.yml` in the
  same change; the reference survived.

Verified as accurate: clang-format job (checks.yml), whitespace
check, CodeQL (repo-level default setup — 30 analyses live;
TODO 32's closure note holds), FreeBSD VM job, the
release-label tagging flow (exercised on every release).

## Fix

CLAUDE.md now states reality: clang-format is THE enforced style
gate; the kernel-style conventions are reviewer-enforced; the CI
section names checks.yml and documents CodeQL's actual mode. A
scripted sweep confirmed every other path in the file resolves
(brace-glob `protobuf_decode.{h,c}` aside).

Docs-only diff.
