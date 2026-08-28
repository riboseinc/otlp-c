# TODO 190 — eleventh review: ADR 0006 enforced (v1.1.15)

**Status:** Complete (v1.1.15)
**Priority:** P2 (architecture enforcement)

## What was wrong

ADR 0006 records the module map as settled — but nothing
checked it. A new internal include edge (the thing that turns
deep modules into a tangle) could appear silently. And
module-table parity was unchecked: the lint's first run caught
common.c, rowless in architecture.md's module table since its
creation.

## The fix

tests/include_lint.py, wired into conformance-gates:
1. The internal include graph is snapshotted in ALLOWED — the
   allowlist IS the architecture. Any edge change fails CI
   until the allowlist AND docs/architecture.md's table are
   updated in one commit (a visible, reviewable decision).
2. Every src/*.c must appear in architecture.md.
3. Stale ALLOWED entries (removed modules) also fail.

Plus: slab.c's relative "../include/otlp-c/allocator.h"
normalized to <otlp-c/allocator.h> (the one public-header
include that didn't use the include path).

## Lesson

An ADR describes a decision; a lint defends it. "Module
boundaries hold" is only true while something notices when they
don't.
