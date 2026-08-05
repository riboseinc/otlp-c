# TODO 17 — Benchmark suite

**Status:** Pending
**Priority:** P2
**Branch:** future (v0.3+)

## Goal

Add a benchmark suite that measures the hot paths: encode a single
span, encode a batch, emit + tick a batch, MPSC queue throughput.
Track regressions across versions.

## Acceptance criteria

- [ ] `bench/bench_encode.c` — encode 1 / 100 / 1000 spans; report ns/op.
- [ ] `bench/bench_emit.c` — emit + tick; report spans/sec.
- [ ] `bench/bench_mpsc.c` — queue throughput under N producers.
- [ ] `bench/README.md` — how to run + interpret results.
- [ ] CI job (nightly) reports benchmark deltas vs main.

## Why

Without numbers, "performance" is guesswork. The benchmarks catch
regressions from refactors (e.g. the arena change in TODO 14).

## Tradeoff

Benchmarks need careful design to be meaningful (warmup, isolation,
deterministic seeds). rushing this produces noise. Defer to v0.3.
