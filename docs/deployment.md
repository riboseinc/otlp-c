# Deployment

`otlp-c` is designed for the **sidecar collector** deployment
pattern: the library emits plain HTTP to a local `otelcol` process,
which terminates TLS, handles auth, queues, retries, and fans out
to one or more cloud backends.

## Why no TLS in the library

The defining invariant of `otlp-c` is **zero non-libc dependencies**.
TLS libraries (OpenSSL, mbedTLS, BoringSSL, GnuTLS) are all third-
party. Platform-native TLS APIs (Security.framework on macOS,
SChannel on Windows) are not part of libc and vary by platform.
Hand-rolling TLS is out of scope.

The sidecar pattern is the only deployment model that satisfies
the invariant while still reaching real cloud backends. It's the
same model OpenTelemetry's own docs describe for constrained
clients, and the same model `opentelemetry-cpp` uses when
configured for plain HTTP.

## Topology

```
┌─────────────────────────┐    plain HTTP    ┌──────────────────┐
│  Host application       │   127.0.0.1:4318  │  Local otelcol   │
│  ┌───────────────────┐  │ ───────────────► │  sidecar process │
│  │  otlp-c (in-proc) │  │                  │  (TLS termination │
│  │  embedded         │  │  ◄────────────── │   + retry + queue │
│  │                   │  │   200 OK / 4xx   │   + fan-out)      │
│  └───────────────────┘  │                  └────────┬─────────┘
└─────────────────────────┘                           │ mTLS / HTTPS
                                                      ▼
                                          ┌────────────────────────┐
                                          │  Remote OTLP backend   │
                                          │  (Tempo / Honeycomb /  │
                                          │   Datadog / Jaeger /   │
                                          │   S3 / etc.)           │
                                          └────────────────────────┘
```

## Production deployment recipes

### VM / bare metal (systemd)

Run otelcol as a systemd unit bound to `127.0.0.1:4318`. Configure
the otelcol exporter to point at the cloud backend with TLS + auth.

```yaml
# /etc/otelcol/config.yaml
receivers:
  otlp:
    protocols:
      http:
        endpoint: 127.0.0.1:4318

exporters:
  otlp/tempo:
    endpoint: tempo.ingest.example.com:443
    tls:
      cert_file: /etc/otelcol/certs/client.pem
      key_file:  /etc/otelcol/certs/client.key
      ca_file:   /etc/otelcol/certs/ca.pem
    headers:
      X-Scope-OrgID: "my-tenant"

service:
  pipelines:
    traces:
      receivers:  [otlp]
      exporters:  [otlp/tempo]
```

### Kubernetes (DaemonSet)

Run otelcol as a DaemonSet on every node. The application pod talks
to the node-local otelcol via the pod network.

```yaml
# Per-pod: point otlp-c at the node-agent otelcol (the base
# endpoint — each signal's path is appended automatically).
OTEL_EXPORTER_OTLP_ENDPOINT=http://$(NODE_IP):4318
```

The agent otelcol forwards to a central collector (or directly to
the cloud backend) over TLS.

### Sidecar container (Kubernetes pod)

For applications that already run a sidecar pattern (e.g. service
mesh), reuse the sidecar for OTLP forwarding:

```yaml
spec:
  containers:
    - name: app
      env:
        - name: OTEL_EXPORTER_OTLP_ENDPOINT
          value: http://127.0.0.1:4318
    - name: otelcol
      image: otel/opentelemetry-collector-contrib:latest
      # ... config binds 127.0.0.1:4318, exports to cloud
```

## Why no direct-to-cloud option

A future 1.x minor version may add an opt-in TLS transport
(`otlp-c-tls`) implemented against a vetted TLS library. That
release will violate the zero-deps invariant; it will be a separate
optional package so the core library stays embeddable. For 0.1.x
the answer is: use the sidecar.

## Caller-tick embedding

The library never spawns a thread. The caller drives I/O via
`otlp_exporter_tick()`. Common embedding patterns:

| Host environment            | Tick pattern                                  |
|-----------------------------|-----------------------------------------------|
| CLI tool, no event loop     | `tick(exp, 0)` after each `emit()`            |
| Game loop / service main    | `tick(exp, 0)` per frame or iteration         |
| Caller-owned worker thread  | caller spawns thread, loops `tick(exp, 100)`  |
| libuv / epoll / kqueue      | `poll_fds()` + tick on event                  |
| Language VM (Node/Python)   | binding wraps tick as a timer callback        |
| Firmware cooperative loop   | `tick(exp, 0)` from main loop                 |

See [examples/minimal.c](../examples/minimal.c) for the simplest
no-event-loop pattern. A `poll_fds()` integration example will
land in a future release.
