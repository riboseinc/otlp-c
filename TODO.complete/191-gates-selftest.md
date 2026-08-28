# TODO 191 — twelfth review: gates self-test (v1.1.16)

**Status:** Complete (v1.1.16)
**Priority:** P2 (the gate suite's own enforcement)

## What was wrong

Eleven reviews built a gate suite; nothing tested the gates.
The class is real: R10 shipped a gate bug (a raw-string escape
that matched nothing) in the same session the gate was written.
A check that silently passes nothing manufactures confidence —
the v0.5.99 lesson, unapplied to the checks themselves.

## The fix

tests/gates_selftest.py, wired into conformance-gates after the
gates: one crafted lie per check branch (9 mutations across
site_docs_sync's five checks and include_lint's two). Each lie
must make its gate exit nonzero AND name the mutated file;
git restores after each. Dirty-tree guard aborts first
(untracked safe — only tracked files mutate/restore).

First run: 8/9 caught; the miss was the LIE's fault (the
module name survived inside the replacement text) — the gate
was right, the mutation was weak. Strengthened, re-caught, 9/9.

## Lesson

Mutation-test the tools you trust, or discover their blind
spots in production. And when a mutation "escapes," audit the
lie before the gate.
