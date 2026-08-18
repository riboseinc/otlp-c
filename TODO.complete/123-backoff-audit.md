# TODO 123 — Retry/backoff audit: jitter, shift UB, 429 bucket

**Status:** Complete (v0.5.83)
**Priority:** P1 (UB fix + doc-vs-code truth + stats accuracy)

## What shipped

Audit of the exporter's retry/backoff machinery (never previously
audited as a unit). Three findings:

**1. `initial << (attempt - 1)` was undefined behavior for large
caller-set max_retries (CWE-190 family).** The shift operated on
`uint32_t` with an unbounded shift count: `max_retries = 100`
(defaults allow it — no cap documented or enforced) shifts by 99.
The wraparound guards (`delay > max || delay < initial`) fired
only AFTER the UB occurred — and cannot fire at all once the shift
count reaches the type width. Fixed by computing the exponent in
`uint64_t` with the shift count clamped at 31, saturating at
`backoff_max_ms`.

**2. The docs promised "full jitter"; the code had none.**
`docs/otlp-spec.md` says (twice) "exponential backoff with full
jitter" — the original design intent (thundering-herd mitigation
for fleets of clients) — but the implementation was deterministic.
Implemented per the documented contract: the delay is now uniform
in `[0, min(initial << (attempt-1), max)]` via a tick-thread-only
xorshift64s PRNG seeded at exporter create (mono clock ^ exporter
address). Upper bound per retry is unchanged; jitter only
randomizes downward — existing timing-based retry tests pass
unchanged.

**3. HTTP 429 was counted in the `http_5xx` bucket.** 429 is
retryable but still a 4xx; honest accounting puts it in
`http_4xx`. (No test pinned the old bucket.) The stats doc
comment now says 4xx includes the retryable 429.

**DRY:** the delay computation existed in two copies (network-
error path and 429/5xx path); both now call one
`backoff_delay_ms(e, attempt)` helper that owns the saturation
math AND the jitter draw.

## Tests

`test_exporter_retry.c` cases 3–4 (assertions via explicit
checks; the suite runs under Release/NDEBUG):

- **429-retry**: 429 then 200 → span sent, `http_4xx >= 1`,
  `http_5xx == 0` — pins the bucket fix and the throttled-retry
  path.
- **shift-guard**: `max_retries = 100`, backoff 1ms/1ms,
  always-500 → drops after 100 retries. Under UBSAN this traps on
  the pre-fix shift (attempt climbs past 32); verified clean with
  the fix. Full UBSAN suite: 38/38.

## Sites changed

- `src/exporter.c` — `backoff_delay_ms` + `jitter_next` helpers;
  jitter PRNG state + seeding; both retry paths on the helper;
  429 → `http_4xx`.
- `include/otlp-c/exporter.h` — stats comment (4xx includes 429).
- `tests/test_exporter_retry.c` — cases 3–4.
- `docs/roadmap.md` — the "full jitter" checkbox, ticked
  (v0.5.83). `docs/otlp-spec.md` needed no change — its claims
  are finally true.

## Verification

```
cmake --build build            # zero warnings
ctest --test-dir build -E http-timeout          # 38/38
cmake -B build-ubsan -DOTLP_C_ENABLE_UBSAN=ON && ctest --test-dir build-ubsan -E http-timeout
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan -E "http-timeout|url-parse"
```
