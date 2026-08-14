# TODO 105 — DNS behavior documentation accuracy

**Status:** Complete (v0.5.65)
**Priority:** P2 (documentation; sets correct performance expectations)

## What shipped

1. **Fixed false DNS-caching claim in `platform.h`.** The
   `otlp_socket_connect` docstring claimed getaddrinfo results
   were "cached at the exporter level for the process lifetime."
   No such caching exists. The claim was aspirational from the
   original design plan but never implemented.

2. **Added public DNS note in `exporter.h`.** The exporter's
   docstring now documents the blocking-DNS behavior and offers
   a mitigation (pre-resolve to IP, or use a tick thread that
   can block briefly).

3. **Comment-accuracy sweep.** Scanned all internal headers for
   strong claims. Verified:
   - "never spawns threads, never takes locks" — accurate.
   - "Cached TCP connection for HTTP keep-alive" — accurate.
   - DNS caching — **false, fixed**.

## Why this matters

A reader of the old comment might assume DNS latency is a
one-time cost and design their tick-loop thread accordingly. In
reality, every reconnect performs a blocking getaddrinfo that
can take seconds on slow or broken DNS. This could cause
unexpected stalls in latency-sensitive callers.

The public note in exporter.h surfaces this to API users who
never read the internal headers.

## DNS behavior summary

- The library does NOT cache DNS results.
- The OS resolver usually does (nscd, systemd-resolved,
  mDNSResponder) — so repeated lookups for the same host are
  typically served from the OS cache.
- With HTTP keep-alive, the connection is reused so DNS
  lookups are rare: one per initial connect plus one per
  reconnect after a failure.
- For callers that cannot tolerate any blocking DNS: resolve
  the collector's hostname to an IP before constructing the
  endpoint, or use a tick thread that can block briefly.

## Verification

```
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build                              # zero warnings
ctest --test-dir build -E http-timeout           # 34/34 pass
```

## Audit context

Continues the documentation-accuracy audit started in v0.5.60
(public API docstrings) and v0.5.64 (roadmap + CLAUDE.md).
v0.5.65 covers internal header comments and adds a public
note about a performance-relevant behavior (blocking DNS).
