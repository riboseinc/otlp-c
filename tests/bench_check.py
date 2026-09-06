#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Bench anti-blowup check: parse a bench binary's output and hold
a metric under a generous ceiling.

Replaces the inline awk field-parsing in ci.yml — the field
index was guessed wrong twice in one session ($4 vs $3, $6 vs
$10). Two rules here:

  1. Unparseable output FAILS the check (a gate that can't
     parse must never silently pass — the v0.5.99 class).
  2. The ceilings are deliberately generous (~20x median):
     this catches algorithmic blowups, not runner noise.

Usage: bench_check.py <metric> <ceiling_ns>   (stdin = output)
Metrics: emit0 (emit pipeline, 0 attrs), encode1 (encode, 1 attr)
"""
import re
import sys

PATTERNS = {
    "emit0": re.compile(
        r"spans=1000\s+attrs=0\s+([\d.]+)\s+ns/span \(emit\)"),
    "encode1": re.compile(
        r"encode\s+100 spans ×\s+1 attrs:\s+\d+ ns total,\s+"
        r"([\d.]+)\s+ns/span"),
}


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in PATTERNS:
        print("usage: bench_check.py <emit0|encode1> <ceiling_ns>",
              file=sys.stderr)
        return 2
    metric, ceiling = sys.argv[1], float(sys.argv[2])
    output = sys.stdin.read()

    m = PATTERNS[metric].search(output)
    if not m:
        print(f"bench-check: FAILED — could not parse '{metric}' from "
              "bench output; a gate that cannot parse must fail",
              file=sys.stderr)
        return 2

    ns = float(m.group(1))
    print(f"bench-check: {metric} = {ns} ns/span "
          f"(ceiling {ceiling:g}) — {'OK' if ns < ceiling else 'BREACH'}")
    return 0 if ns < ceiling else 1


if __name__ == "__main__":
    sys.exit(main())
