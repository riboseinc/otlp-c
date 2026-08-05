# TODO 20 — Add OTLP metrics signal (POST /v1/metrics)

**Status:** Ready
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
