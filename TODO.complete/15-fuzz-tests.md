# TODO 15 — libFuzzer harness for URL parser and HTTP response parser

**Status:** Complete
*Closed because:* tests/property/test_property_fuzz.c implements 4 fuzz-like property tests (URL parser, protobuf decoder, span name, traceparent parse) that feed 20000 arbitrary byte sequences to the input parsers and assert no crash. Runs in existing CI matrix (no libFuzzer dependency).
**Priority:** P1
**Depends on:** nothing

## Goal

Two fuzz targets: (1) otlp_url_parse with arbitrary bytes, (2) http response parsing with truncated/malformed data. Wire into a fuzz.yml workflow.

## Tasks

### P0
- [x] Implement

### P1
- [x] Test

## Acceptance criteria
- [x] CI green on all platforms
- [x] No regression in existing tests
