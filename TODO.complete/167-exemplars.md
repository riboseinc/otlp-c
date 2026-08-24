# TODO 167 — Exemplars (trace-correlated data points)

**Status:** Complete (v0.8.0)
**Priority:** P1 (the last substantive OTLP metrics feature)

## Model + API

- `otlp_exemplar_t` (opaque, metric.h): one value — double
  (field 2) or int64 (field 3), last setter wins; optional
  trace/span correlation (fields 4/5; IDs copied, all-zero
  rejected per the v0.5.54 rule); optional timestamp
  (`otlp_exemplar_mark_time`, field 6). create/free/clone.
- `otlp_metric_add_exemplar(m, ex)`: flat grow-on-demand array in
  the metric (clone-by-value; value required at add time);
  deep-copied by metric_clone; freed by metric_free.

## Wire

- New Exemplar schema table (OTLP_EX_FIELDS: filtered_attributes
  1, double_value 2, int_value 3 sfixed64, trace_id 4, span_id 5,
  time 6) — pinned against upstream literals by unit-wire-numbers
  (CLAUDE.md rule: every new table gets a pin entry).
- Emitted on the data point: NumberDataPoint field 5,
  HistogramDataPoint field 8 (both table entries added; the old
  "not emitted" comments corrected).
- End-to-end emission test: counter with a double+trace+span+time
  exemplar → descend ETSR→RM→SM→Metric→Sum→NDP→exemplars →
  verify each field's number, wire type, and value; histogram
  path covered by the table pins + shared emitter.

## Verification

52/52 via default/release/asan/ubsan/tsan; fresh-configure
zero warnings. The wire test uses only the PUBLIC exemplar API
(the struct stays opaque — first draft poked internals and the
compiler caught it).
