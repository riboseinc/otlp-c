# TODO 95 — Resource attributes array zero-init (UB defense)

**Status:** Complete (v0.5.55)
**Priority:** P1 (memory safety: UB under OOM)

## What shipped

`otlp_exporter_create` allocated the resource_attributes array
with `otlp_malloc` (uninitialized). The fail path then iterated
every slot and freed key/value pointers. Under OOM during
attribute copy, the iteration ran past the failure index into
uninitialized memory — `otlp_free` on garbage pointers is UB.

Fix: switched to `otlp_calloc` so all slots are zero-initialized.
Unset slots have NULL key/value; `otlp_free(NULL)` is a no-op.

## Sites changed

- `src/exporter.c::otlp_exporter_create` — `otlp_malloc` →
  `otlp_calloc` for the resource_attributes array.

## Why this is the v0.5.47 pattern again

v0.5.47 fixed `otlp_attribute_copy_all`'s fail-path: the cleanup
loop freed items 0..i-1 but leaked the failed item i. Same
class of bug: partial-init cleanup that didn't account for which
item failed.

v0.5.55 fixes the resource_attributes path: same shape (loop
that iterates past the failure point) but different consequences
(UB from freeing garbage, vs leak from not freeing). The root
cause is the same: the cleanup loop's bounds didn't match the
actual initialization state.

Both fixes follow the same strategy: make the cleanup safe for
any initialization state, rather than tracking exact bounds.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 50/50 pass

cmake -B build-asan -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_ENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan -E http-timeout      # ASAN clean
```

## Test gap

The bug requires OOM during resource attribute copy to trigger.
Reliably forcing OOM requires a custom allocator hook (still
pending per TODO 28). Without that, the fix is correct by
inspection.

The existing `prop_resource_attrs_*` properties exercise the
success path heavily; the fail path is the untested edge.

## Audit context

Continues the audit pattern:
- v0.5.47: copy_all fail-path leak.
- v0.5.48-v0.5.49: OTLP wire format.
- v0.5.50: LogRecord trace correlation.
- v0.5.51: slab double-free + sampler.
- v0.5.52-v0.5.53: HTTP header injection.
- v0.5.54: ID setter validation.
- v0.5.55: resource_attributes fail-path UB.

The audit pattern has now covered every allocation site in the
codebase for similar partial-init issues. No more known
instances of this bug class.
