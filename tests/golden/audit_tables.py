#!/usr/bin/env python3
"""Audit every table in src/otlp_schema.h against the INSTALLED
opentelemetry-proto descriptors (v1.0.1 lesson: field numbers come
from descriptors, never memory — v0.8.0 shipped an Exemplar table
with five of six numbers wrong).

Run:  python3 tests/golden/audit_tables.py   (needs opentelemetry-proto)

Exit 0 = every table field matches the descriptor's number and
computed wire type. Any "!!" line is a wire bug to fix.
"""
import re
import sys

from opentelemetry.proto.trace.v1 import trace_pb2
from opentelemetry.proto.metrics.v1 import metrics_pb2
from opentelemetry.proto.logs.v1 import logs_pb2
from opentelemetry.proto.common.v1 import common_pb2
from opentelemetry.proto.resource.v1 import resource_pb2
from opentelemetry.proto.collector.trace.v1 import trace_service_pb2
from opentelemetry.proto.collector.metrics.v1 import metrics_service_pb2
from opentelemetry.proto.collector.logs.v1 import logs_service_pb2

# Our table abbreviations -> full descriptor names.
TABLE_TO_MESSAGE = {
    "ETSR": "ExportTraceServiceRequest",
    "RS": "ResourceSpans",
    "R": "Resource",
    "SS": "ScopeSpans",
    "IS": "InstrumentationScope",
    "SPAN": "Span",
    "EVENT": "Event",
    "LINK": "Link",
    "ST": "Status",
    "STATUS": "Status",
    "METRIC": "Metric",
    "EMSR": "ExportMetricsServiceRequest",
    "RM": "ResourceMetrics",
    "SM": "ScopeMetrics",
    "NDP": "NumberDataPoint",
    "SUM": "Sum",
    "GAUGE": "Gauge",
    "HIST": "Histogram",
    "HDP": "HistogramDataPoint",
    "EH": "ExponentialHistogram",
    "EHDP": "ExponentialHistogramDataPoint",
    "EHB": "Buckets",
    "EX": "Exemplar",
    "ELSR": "ExportLogsServiceRequest",
    "EXPSR": "ExportTraceServiceResponse",
    "EPS": "ExportTracePartialSuccess",
    "RL": "ResourceLogs",
    "SL": "ScopeLogs",
    "LOG": "LogRecord",
    "SEVNUM": None,  # enum-only, not a message
    "AV": "AnyValue",
    "KV": "KeyValue",
    "AV_ARRAY": "ArrayValue",
    "KVLIST": "KeyValueList",
}

TYPEMAP = {
    1: "DOUBLE", 2: "FLOAT", 3: "INT64", 4: "UINT64", 5: "INT32",
    6: "FIXED64", 7: "FIXED32", 8: "BOOL", 9: "STRING", 11: "MESSAGE",
    12: "BYTES", 13: "UINT32", 14: "ENUM", 15: "SFIXED32",
    16: "SFIXED64", 17: "SINT32", 18: "SINT64",
}


def wire_for(field):
    # Repeated scalars are packed in proto3 (LEN-delimited)
    # regardless of element type.
    if field.is_repeated and field.message_type is None:
        return "LEN"
    return wire_for_type(field.type)


def wire_for_type(ptype):
    name = TYPEMAP.get(ptype, "VARINT")
    if name in ("STRING", "BYTES", "MESSAGE"):
        return "LEN"
    if name in ("DOUBLE", "FIXED64", "SFIXED64"):
        return "FIXED64"
    if name in ("FLOAT", "FIXED32", "SFIXED32"):
        return "FIXED32"
    return "VARINT"


def load_descriptors():
    descs = {}
    for module in (trace_pb2, metrics_pb2, logs_pb2, common_pb2,
                   resource_pb2, trace_service_pb2,
                   metrics_service_pb2, logs_service_pb2):
        for cname in dir(module):
            cls = getattr(module, cname)
            desc = getattr(cls, "DESCRIPTOR", None)
            if desc is not None and hasattr(desc, "full_name"):
                descs[desc.full_name] = desc
                for nested in getattr(desc, "nested_types_by_name",
                                      {}).values():
                    descs[nested.full_name] = nested
    return descs


def parse_tables(path):
    src = open(path).read()
    tables = {}
    for m in re.finditer(
            r"OTLP_(\w+)_FIELDS\[\] = \{(.*?)\n\};", src, re.S):
        tname, body = m.group(1), m.group(2)
        fields = {}
        for e in re.finditer(
                r'\[OTLP_\w+_FI_\w+\] = \{ "([^"]+)",'
                r"\s*(\d+),\s*OTLP_PB_WIRE_(\w+)", body):
            fields[e.group(1)] = (int(e.group(2)), e.group(3))
        tables[tname] = fields
    return tables


def main():
    descs = load_descriptors()
    by_short = {d.split(".")[-1]: d for d in descs}
    tables = parse_tables("src/otlp_schema.h")
    issues = 0
    for tname, fields in sorted(tables.items()):
        want = TABLE_TO_MESSAGE.get(tname, tname)
        if want is None:
            continue
        full = by_short.get(want)
        if full is None:
            full = next((d for d in descs
                         if d.endswith("." + want)), None)
        if full is None:
            print(f"?? {tname}: no descriptor for {want}")
            issues += 1
            continue
        up = {}
        for f in descs[full].fields:
            up[f.name] = (f.number, wire_for(f))
        for fname, (num, wire) in fields.items():
            if fname not in up:
                print(f"!! {tname}.{fname}: ABSENT upstream ({full})")
                issues += 1
            elif up[fname] != (num, wire):
                print(f"!! {tname}.{fname}: ours=({num},{wire}) "
                      f"upstream={up[fname]}")
                issues += 1
    print(f"{'FAIL' if issues else 'OK'}: {len(tables)} tables, "
          f"{issues} issue(s)")
    return 1 if issues else 0


if __name__ == "__main__":
    sys.exit(main())
