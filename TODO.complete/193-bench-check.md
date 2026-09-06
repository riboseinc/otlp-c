# TODO 193 — fourteenth review: bench_check.py + SPDX (v1.1.18)

**Status:** Complete (v1.1.18)
**Priority:** P2 (closing the twice-paid awk class)

## What was wrong

1. The bench-smoke job parsed bench output with inline awk
   field indices — guessed wrong twice in one session ($4 vs
   $3, then $6 vs $10), caught only by dry-running before
   shipping. The class (fragile inline parsing, silent-pass on
   mismatch) was the exact v0.5.99 lesson, unguarded.
2. The v0.6.14 SPDX sweep covered 135 C files; the five Python
   tools added since (site_docs_sync, include_lint,
   gates_selftest, audit_tables, generate) carried no license
   header — the hygiene quietly stopped covering new file
   types.

## The fix

- tests/bench_check.py: one tested parser for both metrics.
  Unparseable output exits 2 (parse failure = gate failure);
  breach exits 1; good exits 0. Validated on real bench output
  plus all three failure shapes before wiring.
- ci.yml's two inline parse steps replaced by the script.
- gates_selftest.py gains canned-output checks: four samples
  (real emit, real encode, breach, garbage) with required exit
  codes.
- SPDX restored on the five Python tools.
