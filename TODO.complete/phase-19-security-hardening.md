# TODO 19 — Security hardening review

**Status:** Complete (v0.1.x internal audit; external review still scheduled for v1.0)
**Priority:** P0 (for v1.0)
**Branch:** `v0.2-convergence-pass`

## Goal

Audit the codebase for the OWASP-relevant issues a network library
can have. Document findings. Fix the high-severity ones before v1.0.

## Scope

- **SSRF / URL parsing**: `otlp_http_parse_url` accepts any host. For
  the sidecar topology this is fine (caller controls endpoint), but
  document the threat model.
- **Buffer overflows**: every memcpy, snprintf, realloc. Verify bounds.
- **Integer overflow**: size_t arithmetic in encoder, queue, exporter.
- **Malformed response parsing**: HTTP collector returns malformed
  Content-Length / chunked encoding / huge body.
- **DNS rebinding**: getaddrinfo resolves to internal address.
- **Resource exhaustion**: queue fills up; backoff never converges.

## Acceptance criteria

- [ ] `SECURITY-ASSESSMENT.md` documents the audit findings + threat model.
- [ ] All HIGH-severity findings fixed; MEDIUM findings tracked.
- [ ] Property tests added for: malformed responses, oversized bodies, integer overflow in size_t math.
- [ ] External security review requested (Ribose security team).

## Files

- `SECURITY-ASSESSMENT.md` — new.
- `tests/property/test_property_security.c` — new (fuzz-style).

## Why

A library that emits telemetry from inside a security-sensitive
process (libc preload, kernel module) is itself part of the trusted
computing base. The audit must happen before v1.0.
