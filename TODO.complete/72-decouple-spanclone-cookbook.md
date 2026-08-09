# TODO 72 — Code quality: decouple headers + span_clone DRY + cookbook

**Status:** Complete (v0.5.32)
**Priority:** P2 (code quality + documentation)

## What shipped (v0.5.32)

### 1. Decoupled internal_util.h from span_internal.h

v0.5.31 added `#include "span_internal.h"` to `internal_util.h`
for the `otlp_attribute_copy_all` declaration. This coupled the
utility layer to the span layer — every file including
`internal_util.h` transitively pulled in `span_internal.h`.

Replaced with a forward declaration (`struct otlp_attribute;`).
The implementation file still includes the full header. Clean
layered dependency.

### 2. span_clone DRY refactor

`otlp_span_clone` was rebuilt using the PUBLIC API
(`otlp_span_set_attribute_string/int/double/bool/bytes`) for each
attribute — ~47 lines of inline switch logic that checks capacity
and searches for existing keys per attribute. Slow.

Replaced with `otlp_attribute_copy_all` (the shared helper from
v0.5.31): direct struct manipulation, ~7 lines. DRY (same helper
as metric_clone and log_clone). Faster (no capacity check, no key
search).

Also fixed a correctness issue in `otlp_attribute_copy_all`: the
`default: break` case silently set the type but didn't copy the
union value for ARRAY/KVLIST attributes — producing a corrupted
attribute. Now returns error via `goto fail`.

### 3. Cookbook patterns for v0.5.20–v0.5.28

6 new patterns in `docs/cookbook.md`:
- Async metrics and logs (emit_metric_move + tick).
- Production diagnostics (set_logger).
- Resource attributes (typed values).
- W3C Baggage propagation.
- Metric temporality and is_monotonic.
- Configurable flush timeout.

## Acceptance criteria
- [x] internal_util.h uses forward declaration, not full include.
- [x] span_clone uses otlp_attribute_copy_all (DRY).
- [x] otlp_attribute_copy_all fails on ARRAY/KVLIST (correctness).
- [x] Cookbook has 6 new patterns covering v0.5.20–v0.5.28.
- [x] 33/33 tests pass under plain, TSAN, ASAN+UBSAN.
- [x] Zero compiler warnings.
