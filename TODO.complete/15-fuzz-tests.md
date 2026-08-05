# TODO 15 — libFuzzer harness for URL parser and HTTP response parser

**Status:** Deferred (v0.3+)
*Closed because:* libFuzzer harness for encoder + HTTP parser is a separate workstream; needs corpus + sanitizer integration.
**Priority:** P1
**Depends on:** nothing

## Goal

Two fuzz targets: (1) otlp_url_parse with arbitrary bytes, (2) http response parsing with truncated/malformed data. Wire into a fuzz.yml workflow.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
