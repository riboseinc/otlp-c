# TODO 144 — Public API audit + 1.0-readiness assessment

**Status:** Complete (v0.5.104)
**Priority:** P2 (spec completeness for the CNCF-donation track)

## Method

All 124 exported functions across the 10 public headers were
audited with a scripted first pass (flag functions whose
preceding comment lacks return-code, ownership, or thread-safety
annotations) followed by manual triage of every flag — the
script has known false-positive modes (void returns, shared
convention blocks, file-level contracts) that only reading can
dismiss.

## Findings

The surface was largely in shape from v0.5.94 (metric/log return
codes), v0.5.100 (events), and v0.5.103 (UTF-8 contract at the
exporter level). Genuine remainder, all fixed:

- `otlp_exporter_flush_metric`/`flush_log` had no Returns block
  (they live in exporter.h — v0.5.94 covered metric.h/log.h
  only) and no thread-safety note (they block the owner thread;
  TIMEOUT/NETWORK outcomes now documented).
- The three attribute conventions (span/metric/log) documented
  map semantics and OVERFLOW but not the v0.5.103 UTF-8
  contract; added.
- Sampler constructors lacked NULL-on-OOM; added.

Confirmed adequate (spot-checked): allocator/context/w3c/slab/
tracer families; exporter opts boundary validation (batch_size
defaulted+clamped, queue_capacity rounded to pow2); setter
conventions.

## Performance A/B (v0.5.103 vs v0.5.102)

Bench binaries built at both tags on the same host, run back to
back: emit 175–433 ns/span (0–5 attrs, null transport), encode
~170 ns/attr, linear in attribute count. Deltas between the
tags are inside run-to-run noise (one direction reversed across
runs). The event layer and UTF-8 validation cost nothing
measurable; no optimization warranted. An apparent 10× outlier
at 8 attrs in one run was timer granularity — three repeats
showed linear scaling.

## Path to 1.0

Written into the roadmap: five explicit stabilization criteria
(surface audited; wire conformance proven via pins + goldens;
boundary validation complete; no open P1/P2; one additive-only
minor cycle) and the deliberate 1.x exclusions (TLS, gRPC,
compression — the zero-non-libc-dependency invariant) with 2.x
optional-deps as their path.

## Verification

Full suite green (46 tests) in Debug after the header edits; a
mid-edit comment-terminator slip (insertion after `*/` mangled
by clang-format) was caught by the rebuild and fixed.
