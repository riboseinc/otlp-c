#!/usr/bin/env python3
"""Include-graph lint: ADR 0006's claims, enforced.

The module map is settled (ADR 0006, docs/adr/). This lint
snapshots the internal include graph — the allowlist below IS
the architecture — so a new internal include edge cannot appear
silently. Adding an edge is a conscious decision: edit the
allowlist here and the module table in docs/architecture.md in
the same commit, where a reviewer sees it.

Also enforces module-table parity: every src/*.c file appears
in docs/architecture.md (a new module without a table row is a
docs lie from day one).

Exit 0 = graph matches the record. Exit 1 = printed diffs.
"""
import glob
import os
import re
import sys

ALLOWED = {
    "common.c": [],
    "context.c": ["span_internal.h"],
    "env_config.c": ["env_config.h", "http_client.h", "internal_util.h"],
    "exporter.c": [
        "atomic_compat.h", "exporter_internal.h", "exporter_otel.h",
        "http_client.h", "internal_util.h", "log_internal.h",
        "metric_internal.h", "mpsc_queue.h", "otlp_messages.h",
        "platform.h", "retry_policy.h", "span_internal.h",
    ],
    "exporter_otel.c": [
        "exporter_otel.h", "http_client.h", "otlp_messages.h",
        "otlp_schema.h", "protobuf_decode.h", "protobuf_encode.h",
    ],
    "exporter_sync.c": [
        "exporter_internal.h", "http_client.h", "otlp_messages.h",
        "otlp_schema.h", "protobuf_encode.h", "retry_policy.h",
    ],
    "http_client.c": [
        "http_client.h", "http_response_parser.h", "internal_util.h",
        "platform.h",
    ],
    "http_response_parser.c": [
        "http_response_parser.h", "internal_util.h",
    ],
    "internal_util.c": ["internal_util.h", "span_internal.h"],
    "log.c": ["internal_util.h", "log_internal.h", "platform.h"],
    "metric.c": ["internal_util.h", "metric_internal.h", "platform.h"],
    "mpsc_queue.c": [
        "atomic_compat.h", "internal_util.h", "mpsc_queue.h",
    ],
    "otlp_logs_encoder.c": [
        "log_internal.h", "otlp_messages.h", "otlp_schema.h",
        "protobuf_encode.h",
    ],
    "otlp_messages.c": [
        "otlp_messages.h", "otlp_schema.h", "protobuf_encode.h",
        "span_internal.h",
    ],
    "otlp_metrics_encoder.c": [
        "metric_internal.h", "otlp_messages.h", "otlp_schema.h",
        "platform.h", "protobuf_encode.h",
    ],
    "platform.c": ["platform.h"],
    "platform_unix.c": ["internal_util.h", "platform.h"],
    "platform_win.c": ["internal_util.h", "platform.h"],
    "protobuf_decode.c": ["protobuf_decode.h"],
    "protobuf_encode.c": ["internal_util.h", "protobuf_encode.h"],
    "retry_policy.c": ["retry_policy.h"],
    "sampler.c": ["internal_util.h"],
    "slab.c": ["internal_util.h"],  # public header via <otlp-c/...>
    "span.c": ["internal_util.h", "platform.h", "span_internal.h"],
    "tracer.c": [
        "atomic_compat.h", "internal_util.h", "platform.h",
        "span_internal.h",
    ],
    "w3c.c": ["span_internal.h"],
}

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fail = []

src_files = sorted(os.path.basename(f) for f in glob.glob(f"{ROOT}/src/*.c"))

for f in src_files:
    incs = sorted(set(
        re.findall(r'#include\s+"([^"]+)"', open(f"{ROOT}/src/{f}").read())
    ))
    allowed = sorted(ALLOWED.get(f))
    if incs != allowed:
        fail.append(
            f"src/{f} internal includes are {incs}; the recorded graph "
            f"says {allowed}. Adding an edge is an architecture decision: "
            "update ALLOWED here AND docs/architecture.md in one commit."
        )

for f in sorted(set(ALLOWED) - set(src_files)):
    fail.append(f"ALLOWED lists {f}, which no longer exists in src/")

arch = open(f"{ROOT}/docs/architecture.md").read()
missing = [f for f in src_files if f not in arch]
if missing:
    fail.append(
        f"src/ modules with no docs/architecture.md row: {missing}"
    )

if fail:
    print("include-lint: FAILED")
    for f in fail:
        print(f"  - {f}")
    sys.exit(1)
print(
    f"include-lint: OK — {len(src_files)} modules, graph matches "
    "ADR 0006's record, architecture.md table complete"
)
