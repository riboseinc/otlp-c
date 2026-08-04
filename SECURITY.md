# Security Policy

## Reporting a Vulnerability

**Do not open a public issue for security vulnerabilities in otlp-c.**

Email `security@ribose.com` with details:

- A description of the issue.
- The minimum reproduction steps.
- The affected versions (if known).
- Any mitigations you have identified.

## Acknowledgment

We acknowledge receipt within 48 hours. We coordinate disclosure on
a timeline that works for you, defaulting to 90 days unless we agree
otherwise.

## Scope

Vulnerabilities in `otlp-c` source code (in this repository) are in
scope. Vulnerabilities in dependencies (we have none beyond libc)
are not. Vulnerabilities in the OpenTelemetry Protocol itself
belong with the OpenTelemetry project.

Specifically in scope:

- Crashes (segfault, abort, trap) triggered by malformed input to
  the encoder, HTTP client, or exporter.
- Memory leaks that can be triggered by repeated calls.
- Race conditions in the exporter's background thread.
- Buffer overruns in the protobuf wire encoder.

Out of scope:

- "The library is slow" (perf bug, not security).
- "The library doesn't support feature X" (feature request).
- Behavior that matches the [OTLP spec](https://opentelemetry.io/docs/specs/otlp/)
  even if that behavior is surprising.

## Disclosure

Once a fix is ready:

1. We open a private advisory on GitHub.
2. We prepare a patch release.
3. After the patch release ships, we publish the advisory with
   credit (or anonymously if you prefer).

## Hardening

If you're embedding `otlp-c` into a sensitive context, consider:

- Building with `-DOTLP_C_ENABLE_ASAN=ON` and `-DOTLP_C_ENABLE_UBSAN=ON`.
- Reviewing the HTTP client for your threat model. The default
  implementation uses plaintext HTTP/1.1 over raw sockets. For TLS,
  run an `otelcol` on localhost and let it terminate TLS.
- Sandboxing the process running `otlp-c` (seccomp, AppArmor, etc.)
  if untrusted inputs are a concern.
