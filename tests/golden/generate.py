#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate golden wire vectors for otlp-c.

Builds three Export*ServiceRequest payloads with the REFERENCE
opentelemetry-proto Python classes (not our encoder) and writes:

  tests/golden/traces.bin, metrics.bin, logs.bin   (raw payloads,
    inspectable with `protoc --decode_raw < x.bin`)
  tests/golden/golden_vectors.h                    (C byte arrays
    consumed by tests/unit/test_unit_golden.c)

The same fixture values are reconstructed through otlp-c's public
API + internal encoders in test_unit_golden.c, which compares both
sides as canonical protobuf field trees. ANY semantic difference in
our encoding (field numbers, wire types, zigzag, packing, presence,
values) fails the test.

Regenerate (manual; requires: pip install opentelemetry-proto):
    python3 tests/golden/generate.py

Keep the fixture values in this file and in test_unit_golden.c in
lockstep — a mismatch is a test failure, by design.
"""

import hashlib
import os

import opentelemetry.proto.common.v1.common_pb2 as common_pb2
import opentelemetry.proto.logs.v1.logs_pb2 as logs_pb2
import opentelemetry.proto.metrics.v1.metrics_pb2 as metrics_pb2
import opentelemetry.proto.collector.logs.v1.logs_service_pb2 as logs_service
import opentelemetry.proto.collector.metrics.v1.metrics_service_pb2 as metrics_service
import opentelemetry.proto.collector.trace.v1.trace_service_pb2 as trace_service
import opentelemetry.proto.resource.v1.resource_pb2 as resource_pb2
import opentelemetry.proto.trace.v1.trace_pb2 as trace_pb2

SERVICE = "golden-svc"
SCOPE_NAME = "golden-scope"
SCOPE_VERSION = "1.2.3"
T_BASE = 1700000000000000000


def resource():
    return resource_pb2.Resource(
        attributes=[common_pb2.KeyValue(
            key="service.name",
            value=common_pb2.AnyValue(string_value=SERVICE))])


def traces_payload():
    span = trace_pb2.Span(
        trace_id=bytes(range(0x00, 0x10)),
        span_id=bytes(range(0x10, 0x18)),
        parent_span_id=bytes(range(0x20, 0x28)),
        trace_state="vendor1=1,vendor2=2",
        name="golden-span",
        kind=2,  # SpanKind.SERVER
        start_time_unix_nano=T_BASE + 123456789,
        end_time_unix_nano=T_BASE + 987654321,
        attributes=[
            common_pb2.KeyValue(key="http.method",
                                value=common_pb2.AnyValue(string_value="GET")),
            common_pb2.KeyValue(key="http.status_code",
                                value=common_pb2.AnyValue(int_value=404)),
            common_pb2.KeyValue(
                key="neg",
                value=common_pb2.AnyValue(int_value=-1234567890123)),
            common_pb2.KeyValue(key="ratio",
                                value=common_pb2.AnyValue(double_value=0.25)),
            common_pb2.KeyValue(key="ok",
                                value=common_pb2.AnyValue(bool_value=True)),
            common_pb2.KeyValue(
                key="raw",
                value=common_pb2.AnyValue(bytes_value=b"\x00\x01\xfe\xff")),
        ],
        events=[trace_pb2.Span.Event(
            time_unix_nano=T_BASE + 500000000,
            name="evt",
            attributes=[common_pb2.KeyValue(
                key="k",
                value=common_pb2.AnyValue(string_value="v"))])],
        links=[trace_pb2.Span.Link(
            trace_id=bytes(range(0x30, 0x40)),
            span_id=bytes(range(0x40, 0x48)),
            attributes=[common_pb2.KeyValue(
                key="linked",
                value=common_pb2.AnyValue(bool_value=True))])],
        flags=1,  # W3C sampled — otlp-c spans default to sampled
        status=trace_pb2.Status(code=2, message="boom"))  # ERROR
    return trace_service.ExportTraceServiceRequest(
        resource_spans=[trace_pb2.ResourceSpans(
            resource=resource(),
            scope_spans=[trace_pb2.ScopeSpans(
                scope=common_pb2.InstrumentationScope(
                    name=SCOPE_NAME, version=SCOPE_VERSION),
                spans=[span])])])


def metrics_payload():
    eh_dp = metrics_pb2.ExponentialHistogramDataPoint
    EHB = eh_dp.Buckets
    m = metrics_pb2
    gauge = metrics_pb2.Metric(
        name="golden-gauge", unit="ms", description="gd",
        gauge=metrics_pb2.Gauge(data_points=[metrics_pb2.NumberDataPoint(
            time_unix_nano=T_BASE + 555000000,
            as_double=42.5,
            attributes=[common_pb2.KeyValue(
                key="axis",
                value=common_pb2.AnyValue(string_value="x"))])]))

    # All values are exact in binary — sums match bit-for-bit in any
    # implementation. records: [1.25, 12.5, 15, 22, 25, 30, 64]
    # -> count=7 sum=169.75 min=1.25 max=64 buckets=[1,2,3,1]
    hist = metrics_pb2.Metric(
        name="golden-hist", unit="B",
        histogram=metrics_pb2.Histogram(
            aggregation_temporality=2,  # CUMULATIVE
            data_points=[metrics_pb2.HistogramDataPoint(
                start_time_unix_nano=T_BASE,
                time_unix_nano=T_BASE + 111111111,
                count=7,
                sum=169.75,
                bucket_counts=[1, 2, 3, 1],
                explicit_bounds=[10.5, 20.5, 30.5],
                min=1.25,
                max=64.0)]))

    # 8 nonzero records (8+16+32+0.5+4+2+1+36 = 99.5) + 2 zero
    # records -> count=10 zero_count=2 sum=99.5. Buckets set
    # explicitly (otlp-c keeps record() and set_exp_histogram()
    # independent).
    eh = metrics_pb2.Metric(
        name="golden-eh",
        exponential_histogram=metrics_pb2.ExponentialHistogram(
            aggregation_temporality=2,  # CUMULATIVE
            data_points=[metrics_pb2.ExponentialHistogramDataPoint(
                time_unix_nano=T_BASE + 222222222,
                count=10,
                sum=99.5,
                scale=-3,
                zero_count=2,
                positive=EHB(offset=4, bucket_counts=[1, 0, 2]),
                negative=EHB(offset=-2, bucket_counts=[5]))]))

    counter = metrics_pb2.Metric(
        name="golden-counter", unit="1",
        sum=metrics_pb2.Sum(
            aggregation_temporality=2,  # CUMULATIVE
            is_monotonic=True,
            data_points=[metrics_pb2.NumberDataPoint(
                time_unix_nano=T_BASE + 333333333,
                as_double=7.0)]))

    return metrics_service.ExportMetricsServiceRequest(
        resource_metrics=[metrics_pb2.ResourceMetrics(
            resource=resource(),
            scope_metrics=[metrics_pb2.ScopeMetrics(
                scope=common_pb2.InstrumentationScope(
                    name=SCOPE_NAME, version=SCOPE_VERSION),
                metrics=[gauge, hist, eh, counter])])])


def logs_payload():
    return logs_service.ExportLogsServiceRequest(
        resource_logs=[logs_pb2.ResourceLogs(
            resource=resource(),
            scope_logs=[logs_pb2.ScopeLogs(
                scope=common_pb2.InstrumentationScope(
                    name=SCOPE_NAME, version=SCOPE_VERSION),
                log_records=[
                    logs_pb2.LogRecord(
                        time_unix_nano=T_BASE + 222222222,
                        severity_number=9,  # INFO
                        severity_text="INFO",
                        body=common_pb2.AnyValue(string_value="golden message"),
                        attributes=[
                            common_pb2.KeyValue(
                                key="k",
                                value=common_pb2.AnyValue(string_value="v")),
                            common_pb2.KeyValue(
                                key="code",
                                value=common_pb2.AnyValue(int_value=404)),
                        ],
                        trace_id=bytes(range(0x50, 0x60)),
                        span_id=bytes(range(0x60, 0x68))),
                    logs_pb2.LogRecord(
                        time_unix_nano=T_BASE + 333333333,
                        severity_number=13,  # WARN
                        severity_text="WARN",
                        body=common_pb2.AnyValue(string_value="second")),
                ])])])


def c_array(name, data):
    body = ", ".join(f"0x{b:02x}" for b in data)
    wrapped = "\n".join(
        "    " + part for part in
        [body[i:i + 96] for i in range(0, len(body), 96)])
    define = "#define " + name + "_LEN ((size_t) sizeof(" + name + "))\n"
    return ("static const unsigned char " + name + "[] = {\n"
            + wrapped + "};\n" + define)


def main():
    out_dir = os.path.dirname(os.path.abspath(__file__))
    vectors = [
        ("GOLDEN_TRACES", traces_payload().SerializeToString()),
        ("GOLDEN_METRICS", metrics_payload().SerializeToString()),
        ("GOLDEN_LOGS", logs_payload().SerializeToString()),
    ]

    header = [
        "/* SPDX-License-Identifier: Apache-2.0 */",
        "/* GENERATED by tests/golden/generate.py from",
        " * opentelemetry-proto 1.44.0 reference serialization.",
        " * Do not edit by hand — regenerate with the script. */",
        "#ifndef OTLP_C_GOLDEN_VECTORS_H",
        "#define OTLP_C_GOLDEN_VECTORS_H",
        "",
        "#include <stddef.h>",
        "",
    ]
    for name, data in vectors:
        bin_path = os.path.join(out_dir, name[len("GOLDEN_"):].lower() + ".bin")
        with open(bin_path, "wb") as f:
            f.write(data)
        digest = hashlib.sha256(data).hexdigest()
        print(f"{name}: {len(data)} bytes  sha256={digest[:16]}…")
        print(f"  -> {bin_path}")
        header.append(c_array(name, data))
        header.append("")
    header += ["#endif", ""]

    with open(os.path.join(out_dir, "golden_vectors.h"), "w") as f:
        f.write("\n".join(header))
    print("  -> tests/golden/golden_vectors.h")


if __name__ == "__main__":
    main()
