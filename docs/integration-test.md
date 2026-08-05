# Integration test (Phase 6)

The Phase 6 integration test verifies that spans emitted by otlp-c
show up in a real OpenTelemetry collector and Jaeger backend.

## Topology

This mirrors the recommended production deployment (see
[deployment.md](../docs/deployment.md)):

```
test binary (otlp-c in-proc)
   │  plain HTTP, 127.0.0.1:4318
   ▼
otelcol container (TLS termination + queue + retry in production)
   │  OTLP/gRPC, jaeger:4317
   ▼
Jaeger all-in-one container (storage + UI + query API on :16686)
```

In production the Jaeger service is replaced with a real
TLS-terminated cloud backend (Tempo, Honeycomb, Datadog, etc.).

## Running locally

```sh
# 1. Bring up the otelcol + Jaeger containers.
cd tests/integration
docker compose up -d
cd ../..

# 2. Build with tests enabled.
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON
cmake --build build

# 3. Run the integration test.
OTLP_C_RUN_INTEGRATION=1 ctest --test-dir build -L integration --output-on-failure

# 4. Inspect traces in the Jaeger UI:
#    http://localhost:16686/search?service=otlp-c-integration-test

# 5. Clean up.
cd tests/integration && docker compose down && cd ../..
```

The test gates itself on the `OTLP_C_RUN_INTEGRATION` env var so
that running `ctest` without Docker installed does not fail local
dev runs.

## CI

`.github/workflows/build.yml` runs the integration test on Linux
and macOS, after a `docker compose up -d` step. Windows is
deferred (Docker-on-Windows CI is more complex).
