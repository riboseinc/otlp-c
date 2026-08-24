# TODO 159 — Retry policy as pure, property-tested functions

**Status:** Complete (v0.6.13)
**Priority:** P2 (invariant coverage)

## The friction

Retry timing lived as private helpers inside exporter.c
(`now_mono_ms`, `jitter_next`, `backoff_delay_ms`) plus an inline
Retry-After clamp in record_outcome's 429/5xx branch. The timing
invariants — full-jitter bounds, the shift-count clamp, the
Retry-After floor vs backoff-max cap — were verified only through
exporter scenario tests driving the whole pipeline. A private
millisecond clock also existed twice (exporter.c's `now_mono_ms`,
http_client.c's `mono_ms`).

## What shipped

- **`src/retry_policy.{h,c}`**: `otlp_retry_base_delay_ms`
  (ceiling with the shift clamp), `otlp_retry_delay_ms`
  (uniform draw, never below the server floor, never above our
  cap; optional `server_driven` out-param), `otlp_jitter_next`.
  Pure functions; logic extracted verbatim, behavior unchanged.
- The 429/5xx inline clamp collapsed into the decision function;
  the network-error branch calls it with floor 0.
- **One clock**: `otlp_platform_now_mono_ms()` in platform;
  both private copies deleted.
- **`property-retry` (test #51)**: jitter bounds vs the computed
  ceiling; attempt=UINT32_MAX saturates at max (no UB shift);
  delay ≥ min(floor, max) and ≤ max always; `server_driven`
  exactly equals "floor > drawn jitter", verified by replaying
  the saved PRNG state; xorshift nonzero/determinism.

## Verification

- 51/51 in Debug, Release (zero warnings), ASAN+leaks, UBSAN,
  TSAN.
- **Mutation-tested**: deleting the floor lift fails the property
  at the floor invariant (line-pinned by check_true).
