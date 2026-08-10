# TODO 78 — Pre-size encode buffers (performance)

**Status:** Complete (v0.5.38)
**Priority:** P2 (performance optimization)

## What shipped

Pre-sized the protobuf body buffer at encode time based on the
batch size. Eliminates ~10 malloc+memcpy+free growth cycles per
full batch (512 items).

## Sites changed

- `exporter_otel.c::otlp_exporter_otel_build_request` — traces:
  `n_spans * 256 + 1024`.
- `exporter.c::try_start_metric_post` — metrics:
  `metric_pending_count * 128 + 512`.
- `exporter.c::try_start_log_post` — logs:
  `log_pending_count * 128 + 512`.

The synchronous flush paths encode 1 item at a time — pre-sizing
is negligible and was left unchanged.
