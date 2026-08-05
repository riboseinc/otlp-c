# TODO 26 — Add OTLP/gRPC transport option

**Status:** Ready
**Priority:** P1
**Depends on:** nothing

## Goal

Lower latency than HTTP/1.1 for high-volume deployments. Needs HTTP/2 client (hand-rolled or minimal vendored). Option in exporter: OTLP_TRANSPORT_GRPC.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
