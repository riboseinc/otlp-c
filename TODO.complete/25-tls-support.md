# TODO 25 — OS-native TLS for HTTPS endpoints

**Status:** Ready
**Priority:** P1
**Depends on:** nothing

## Goal

macOS Secure Transport, Windows Schannel, optional OpenSSL on Linux. New option in otlp_exporter_opts_t: use_tls, ca_cert_path.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
