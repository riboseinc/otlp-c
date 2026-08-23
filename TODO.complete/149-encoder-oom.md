# TODO 149 — Encoder OOM-propagation coverage (fail-injection into the encode paths)

**Status:** Complete (v0.6.3)
**Priority:** P3 (closes the out-of-scope note from 148)

## What shipped

Three cases in the fail-injecting allocator suite
(`tests/test_allocator_oom.c`): encode OOM probes for traces,
metrics, and logs. Rich fixtures (all attribute types, histogram
+ exp-histogram, events/links, long strings) are built with the
default allocator, then every allocation position in the encode
path is failed one at a time (100/120/100 probes), asserting:

- the result is OTLP_OK or OTLP_ERR_NOMEM — never another code,
  never a crash (ASAN gates the UB case),
- alloc_count == free_count after every iteration — every
  successful allocation is paired with a free on every failure
  path (the goto-out cleanup arms).

**The unlock — small-buffer optimization**: the first cut
enlarged nothing and the coverage number did not move. The pb
buffers keep a 64-byte INLINE buffer: tiny fixtures never
allocate, so mid-emission reserve failures were unreachable —
the probes only ever saw the early buf_init failure or nothing.
Fixtures now carry 160-byte attribute values, pushing every
sub-buffer to the heap; the mid-path arms fire and the metrics
encoder's line coverage rises 82.09% → 84.45% (traces/logs
encoders also up). Every library file remains ≥ 82%.

Result: the encoders' OOM cleanup paths are proven correct —
no leaks and clean NOMEM propagation at every failure position.

## Verification

49/49 tests in all five configurations; fresh Release tree zero
warnings; coverage re-measured post-change.
