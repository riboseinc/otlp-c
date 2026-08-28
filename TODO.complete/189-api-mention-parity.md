# TODO 189 — tenth review: API-mention parity (v1.1.14)

**Status:** Complete (v1.1.14)
**Priority:** P2 (site correctness — readers copy this code)

## What was wrong

Prototyping the check found real drift before the gate shipped:

1. /docs/signals showed
   `otlp_metric_set_exp_histogram_bounds(h, bounds, 3)` — no
   such function exists; bounds are an otlp_metric_create()
   parameter.
2. /examples showed `otlp_exporter_poll_events()` and called
   `otlp_exporter_poll_fds()` with a bare fd — the real API
   fills an `otlp_poll_fd_t {fd, events}`.

Readers copying either snippet got broken builds.

## The fix

- Both snippets corrected; both compile against the real
  headers under -Wall -Wextra -Werror (verified in /tmp).
- site_docs_sync.py section 4: every otlp_* mention on
  reader-facing surfaces (website pages, quickstart, cookbook,
  README) must exist in the public headers or the explicit
  allowlist. Internal/history docs excluded by design — gating
  them would need an allowlist as wide as the check.

## Lesson

Parity gates should cover every direction a reader can be
lied to: env vars (v1.1.9), versions/tags (v1.1.13), and the
API names themselves (this). The API freeze makes this class
rare — which is exactly why nothing had ever checked it.
