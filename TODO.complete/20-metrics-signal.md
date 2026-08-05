# TODO 20 — Add OTLP metrics signal (POST /v1/metrics)

**Status:** Deferred (post-1.0)
*Closed because:* OTLP metrics signal is a separate major workstream. v0.x is traces-only.
**Priority:** P1
**Depends on:** nothing

## Goal

Implement Counter, Histogram, Gauge types. New public API: otlp_metric_create, otlp_metric_add, otlp_metric_record. Reuse protobuf_encode.c for wire format.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
