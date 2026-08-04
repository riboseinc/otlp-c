/* SPDX-License-Identifier: Apache-2.0 */
/*
 * OTLP/HTTP exporter — STUB.
 *
 * Phase 5 of the roadmap. Real implementation will:
 *
 *   1. Accept a batch of spans (struct otlp_span[]).
 *   2. Wrap them in ResourceSpans -> ScopeSpans -> Span.
 *   3. Encode via protobuf_encode.c into a flat buffer.
 *   4. POST the buffer to /v1/traces via http_client.c.
 *   5. Decode the ExportTraceServiceResponse (most failures use
 *      the status field).
 *
 * This is internal — the public API is exporter.h.
 */
#include <otlp-c/exporter.h>

/* TODO Phase 5: real encoding + POST. */
