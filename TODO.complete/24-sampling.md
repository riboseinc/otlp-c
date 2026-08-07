# TODO 24 — Head-based sampling

**Status:** Complete (v0.5)
**Priority:** P1
**Depends on:** nothing

## Goal

Pluggable sampler that decides at span-creation time whether to
record. Built-in policies + custom vtable for caller-supplied
samplers.

## What shipped (v0.5)

**Public API** (`include/otlp-c/sampler.h`):
- `otlp_sampling_decision_t` enum: NOT_RECORD / RECORD /
  RECORD_AND_SAMPLED.
- `otlp_sampling_result_t` struct (decision + future attributes
  extension point).
- `otlp_sampler_t` vtable: should_sample + free function pointers.
- `otlp_sampler_always_on()` — stateless singleton, returns
  RECORD_AND_SAMPLED.
- `otlp_sampler_always_off()` — stateless singleton, returns
  NOT_RECORD.
- `otlp_sampler_trace_id_ratio_based(ratio)` — deterministic on
  the first 8 bytes of trace_id. Same trace_id always yields the
  same decision, so downstream services agree.
- `otlp_sampler_free()` — releases the sampler (no-op for the
  static singletons).

**Tracer integration** (`include/otlp-c/tracer.h`):
- New `otlp_tracer_set_sampler(tracer, sampler)` sets a sampler
  at any time. Pass NULL to revert to always_on (default).
- `start_span` consults the sampler with the freshly-generated
  trace_id. NOT_RECORD means start_span frees the in-progress span
  and returns NULL. RECORD_AND_SAMPLED sets the span's sampled
  flag (the default). RECORD clears it.

**Span accessor** (`include/otlp-c/span.h`):
- `otlp_span_is_sampled()` now publicly exposed (was internal).
  Symmetric with `otlp_span_set_sampled()`.

**Implementation** (`src/sampler.c`):
- Stateless always_on/off as static singletons (no allocation).
- Ratio sampler allocated on the heap; stores the clamped ratio.

**Property tests** (`tests/property/test_property_sampler.c`):
- `prop_always_on_always_samples` — every span sampled.
- `prop_always_off_never_samples` — every span dropped.
- `prop_ratio_zero_drops_all` — ratio 0 drops everything.
- `prop_ratio_one_keeps_all` — ratio 1 keeps everything.
- `prop_ratio_deterministic` — same trace_id → same decision (100 iters).
- `prop_ratio_distribution` — ratio 0.5 ≈ 500/1000 kept (loose bound).
- `prop_default_sampler_is_always_on` — start_span returns sampled span.

## Design notes

Head sampling is chosen over tail sampling because it's the cheapest
place to drop a span — no work is done on dropped spans. Tail sampling
(re-deciding at end_time based on attributes/duration) is deferred;
it requires a different API surface (decision at emit time, not
start time) and is much harder to do correctly with the caller-tick
exporter model. The current API supports head sampling only.

The ratio-based sampler is deterministic on trace_id (not random per
call) so that distributed traces sample consistently across services.
This matches the W3C Trace Context model: sampling decisions
propagate via the traceparent's sampled flag.

## Acceptance criteria
- [x] CI green on macOS arm64.
- [x] No regression in existing tests.
- [x] Property tests pass deterministically.

## Out of scope (deferred)
- Tail sampling (decide at end_time).
- Sampler-provided attributes (e.g., a sampler that tags every
  kept span with "sampledBy=ratio50").
- Parent-based sampler (consult parent's sampled flag).
- Custom sampler registration by name (registry pattern).
