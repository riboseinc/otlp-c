# Architecture Decision Records (ADRs)

This directory captures the load-bearing design decisions for `otlp-c`.
Each ADR explains the context, the choice, the alternatives weighed,
and the consequences. The ADR template is the lightweight MADR form
(Status, Context, Decision, Consequences).

When you read a piece of code and ask "why is it this way?", the
answer should be in one of these documents. If you make a change
that contradicts an ADR, either update the ADR first or open a
discussion about it.

Index:

- [0001 — Zero non-libc dependencies](0001-zero-non-libc-deps.md)
- [0002 — Caller-tick, no library threads](0002-caller-tick-no-threads.md)
- [0003 — Vyukov bounded MPSC queue](0003-mpsc-via-vyukov.md)
- [0004 — otlpcol sidecar for TLS](0004-otlpcol-sidecar-tls.md)
- [0005 — C11 atomics only for the MPSC queue](0005-c11-atomics-only-for-mpsc.md)
