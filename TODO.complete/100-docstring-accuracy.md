# TODO 100 — Public API docstring accuracy

**Status:** Complete (v0.5.60)
**Priority:** P3 (documentation; no code change)

## What shipped

Updated several public API docstrings in `span.h` and `log.h`
that had drifted from the actual library behavior:

- `otlp_span_set_parent_span_id`: previously claimed "Empty
  (8 zero bytes) for a root span." Wrong post-v0.5.54 — all-zero
  is now rejected. Doc now states NULL clears the parent;
  non-NULL all-zero returns INVALID_ARGUMENT.
- `otlp_span_set_trace_id` / `_span_id`: didn't mention all-zero
  rejection (added v0.5.54). Now documented with reference to
  W3C Trace Context §3.1.1/§3.1.2.
- `otlp_span_set_start_time` / `_end_time`: previously claimed
  "exporter refuses to emit a span with start_time = 0." False
  — the library emits whatever value is set. Doc now states
  this accurately and points to `mark_start` / `mark_end` for
  the "set to now" convenience.
- `otlp_log_record_set_trace_id` / `_span_id`: didn't mention
  the v0.5.50 split (independent has_trace_id / has_span_id
  flags) or v0.5.54 all-zero rejection. Now documented.

## Why this matters

API docs that lag the implementation cause integration bugs.

A caller reading "Empty for a root span" might pass 8 zero
bytes and get an unexpected `INVALID_ARGUMENT`. A caller
reading "exporter refuses to emit start_time = 0" might add
workarounds for behavior that doesn't exist.

The v0.5.48-v0.5.59 audit arc fixed many bugs but didn't
update all the docs. This release catches the docs up.

## No code changes

This release is documentation only. No behavior change.
Existing tests still pass.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 34/34 pass
```

## Audit context

The v0.5.48-v0.5.59 audit arc fixed 27+ bugs across wire
format, W3C spec, memory safety, security, and accounting.
Many of those fixes changed public API behavior (added
validation, changed defaults, etc.) without updating the
docstrings. v0.5.60 closes that loop.

This is a milestone: **TODO #100 complete**. The project has
shipped 100 substantial TODOs across phases 0-22 plus the
v0.5.x audit arc.
