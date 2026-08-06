# TODO 43 — Architecture: model-driven attribute dispatch

**Status:** Complete
*Closed because:* enum otlp_attr_type reordered to match AnyValue schema indices. encode_any_value refactored from 5-case switch to table dispatch via attr_encoders[]. Adding a new attribute type is now a one-line table entry + one function — no switch to modify. OCP achieved.
**Priority:** P1
**Branch:** future (v0.3+)

## Goal

Currently `struct otlp_attribute` is a tagged union with a closed enum
(`OTLP_ATTR_STRING`, `_INT64`, `_DOUBLE`, `_BOOL`, `_BYTES`). The
encoder dispatches with a `switch`. Adding a new attribute type
(currently disabled — ArrayValue, KeyValueList) means editing the
enum, the union, the setter, the encoder, and all the property tests.

**Model-driven** approach: encode the attribute type as records whose
fields are the encoder primitives. The encoder iterates the records
without per-type code.

## Sketch

```c
struct otlp_attr_descriptor {
    const char *name;                  /* "string_value", "int_value", ... */
    uint32_t    field_number;          /* 1..7 in AnyValue */
    void      (*encode)(struct otlp_pb_buf *, const void *);
    size_t     size;                   /* sizeof the variant */
};

extern const struct otlp_attr_descriptor OTLP_AVT_DESCRIPTORS[];
extern const size_t OTLP_AVT_DESCRIPTORS_N;

struct otlp_attribute {
    enum otlp_attr_type type;          /* discriminator */
    union {
        char   *string_val;
        int64_t int64_val;
        double  double_val;
        bool    bool_val;
        struct { uint8_t *data; size_t len; } bytes_val;
    } v;
};
```

Then `otlp_encode_any_value(buf, attr)` is:
```c
const struct otlp_attr_descriptor *d = &OTLP_AVT_DESCRIPTORS[attr->type];
otlp_pb_tag(buf, d->field_number, OTLP_PB_WIRE_LEN);
if (d->encode) d->encode(buf, &attr->v);
else           otlp_pb_bytes(buf, attr->v.bytes_val.data, attr->v.bytes_val.len);
```

## Acceptance criteria

- [ ] `src/otlp_attributes.h` defines the descriptor table.
- [ ] `otlp_encode_any_value` becomes a generic loop over the descriptor.
- [ ] Adding a new attribute type (e.g. ArrayValue) is a one-line table
  entry plus a per-type encode function. No `switch` change.
- [ ] All existing property tests still pass.
- [ ] ASAN-clean.

## Why

OCP — open for extension, closed for modification. The current
closed-enum dispatch is the antithesis of OCP.

## Reference

Functional-programming "open sum types" — the same idea.
