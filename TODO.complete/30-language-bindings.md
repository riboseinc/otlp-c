# TODO 30 — Python, Rust, Go bindings for otlp-c

**Status:** Out of scope (this repo)
*Closed because:* Language bindings live in separate repos (otlp-c-python, otlp-c-ruby, etc.). Cookbook documents the integration pattern.
**Priority:** P1
**Depends on:** nothing

## Goal

Python via cffi (PyPI package otlp-c). Rust via repr(C) (crates.io). Go via cgo (pkg.go.dev). Each binding wraps the C API idiomatically.

## Tasks

### P0
- [ ] Implement

### P1
- [ ] Test

## Acceptance criteria
- [ ] CI green on all platforms
- [ ] No regression in existing tests
