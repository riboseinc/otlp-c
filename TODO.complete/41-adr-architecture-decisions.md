# TODO 41 — Architecture decision records (ADRs)

**Status:** Complete
**Priority:** P2
**Branch:** `v0.2-final-pass`

## Goal

Capture the load-bearing design decisions as ADRs in `docs/adr/`. Each
ADR explains the context, the choice made, the alternatives considered,
and the consequences. Readers (especially future contributors) should
be able to understand WHY the code is the way it is.

## Acceptance criteria

- [x] `docs/adr/` directory created.
- [x] `docs/adr/0001-zero-non-libc-deps.md` — why we don't vendor protobuf-c, libcurl, or OpenSSL.
- [x] `docs/adr/0002-caller-tick-no-threads.md` — why the library never spawns threads.
- [x] `docs/adr/0003-mpsc-via-vyukov.md` — why Vyukov bounded MPSC over simpler alternatives.
- [x] `docs/adr/0004-otlpcol-sidecar-tls.md` — why HTTPS terminates outside the library.
- [x] `docs/adr/0005-c11-atomics-only-for-mpsc.md` — why C11 is needed despite CLAUDE.md's C99 baseline.
- [x] `docs/adr/README.md` — index + template (`Status`, `Context`, `Decision`, `Consequences`).
- [x] `docs/architecture.md` cross-references the ADRs.

## Why

Decisions rot without rationale. "Why don't we just use std::thread?"
becomes a stupid question in 2027 if the answer "because the library
is zero-deps and no-threads" is captured in an ADR.

## Reference

Inspired by https://github.com/adr/madr (Markdown ADR).
