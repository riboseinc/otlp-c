# TODO 21 — Add OTLP logs signal (POST /v1/logs)

**Status:** Deferred (post-1.0)
*Closed because:* OTLP logs signal is a separate major workstream. v0.x is traces-only.
**Priority:** P1
**Depends on:** nothing

## Goal

Implement LogRecord type. New public API: otlp_log_emit. Reuse protobuf_encode.c.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
