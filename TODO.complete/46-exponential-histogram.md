# TODO 46 — ExponentialHistogram metric type

**Status:** Complete (v0.5.5)
**Priority:** P2
**Depends on:** nothing

## Goal

Implement the `ExponentialHistogram` metric type from
opentelemetry-proto. Currently the metric encoder handles Counter,
Gauge, and Histogram; ExponentialHistogram is the last standard
metric type missing.

## Background

ExponentialHistogram is more compact than fixed-bucket Histogram at
high resolution. Instead of explicit bucket boundaries, it uses a
base-2 scale: bucket index `i` covers `[2^(i-1)/scale, 2^i/scale)`.
This is useful for latency histograms where tail latency matters.

The library already has the dispatch infrastructure:
`metric_kind_specs[]` table in `src/otlp_metrics_encoder.c`. Adding
a new metric type is one schema entry + one encoder function + one
table entry. OCP.

## Design

**Schema** (`src/otlp_schema.h`):
```c
enum { OTLP_METRIC_FI_EXP_HISTOGRAM = OTLP_METRIC_FI_HISTOGRAM + 1 };
OTLP_METRIC_FIELDS[OTLP_METRIC_FI_EXP_HISTOGRAM] =
    {"exponential_histogram", 10, OTLP_PB_WIRE_LEN, ...};

enum { OTLP_EH_FI_DATA_POINTS, OTLP_EH_FI_AGG_TEMP, OTLP_EH_FI_COUNT };
OTLP_EH_FIELDS[] = {
    {"data_points", 1, OTLP_PB_WIRE_LEN, ..., true},
    {"aggregation_temporality", 2, OTLP_PB_WIRE_VARINT, ..., false},
};

enum { OTLP_EHDP_FI_ATTRIBUTES, OTLP_EHDP_FI_START_TIME, OTLP_EHDP_FI_TIME,
       OTLP_EHDP_FI_COUNT, OTLP_EHDP_FI_SUM, OTLP_EHDP_FI_SCALE,
       OTLP_EHDP_FI_ZERO_COUNT, OTLP_EHDP_FI_POSITIVE, OTLP_EHDP_FI_NEGATIVE,
       OTLP_EHDP_FI_FLAGS, OTLP_EHDP_FI_NFIELDS };
```

**Encoder** (`src/otlp_metrics_encoder.c`):
```c
static otlp_status_t
emit_exp_histogram_data_point(struct otlp_pb_buf *parent,
                              uint32_t field_num,
                              const otlp_metric_t *m)
{
    /* Fields: attributes, start_time, time, count, sum, scale,
     * zero_count, positive_bucket{offset,bitmap}, negative_bucket, flags. */
}
```

**Dispatch table entry**:
```c
[OTLP_METRIC_EXP_HISTOGRAM] = { OTLP_METRIC_FI_EXP_HISTOGRAM,
                                 emit_exp_histogram_data_point },
```

**Public API** (`include/otlp-c/metric.h`):
```c
typedef enum {
    OTLP_METRIC_COUNTER = 1,
    OTLP_METRIC_GAUGE = 2,
    OTLP_METRIC_HISTOGRAM = 3,
    OTLP_METRIC_EXP_HISTOGRAM = 4,  /* new */
} otlp_metric_type_t;
```

## Acceptance criteria
- [ ] Schema entries added.
- [ ] `emit_exp_histogram_data_point` encoder implemented.
- [ ] Dispatch table entry populated.
- [ ] `struct otlp_metric` extended with scale, zero_count, positive/negative buckets.
- [ ] `otlp_metric_record` updates the right bucket via scale.
- [ ] Property test for field numbers + value round-trip.
- [ ] CI green on all 7 platforms.

## Out of scope (deferred further)
- `record()` API for high-cardinality use cases (current API takes a value).
- Aggregation temporality other than CUMULATIVE.
