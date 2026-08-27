# TODO 187 — eighth review: property harness deepened (v1.1.12)

**Status:** Complete (v1.1.12)
**Priority:** P2 (test harness depth)

## What was wrong

The property CMake was 25 near-identical blocks. TIMEOUT 60
everywhere forced the weekly soak off ctest into a shell loop
with a hand-maintained exclude list — a shallow harness that
spawned real workarounds.

## The fix

otlp_add_property_test(): SOURCES, THREADS, TIMING, RUN_SERIAL,
INCLUDES. Labels soak (TIMEOUT 600) vs timing (TIMEOUT 60).
Soak workflow: OTLP_C_PROPERTY_ITERS=100000 ctest -L soak.

Validated: 21 soak / 4 timing labels; 25/25 property; ctest -L
soak at 1000 iters green.
