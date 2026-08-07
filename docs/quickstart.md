# Quickstart

This guide walks through installing `otlp-c`, building your first
program, and seeing the emitted span in a local collector + Jaeger.

## 1. Install

`otlp-c` builds with CMake 3.20+ and a C11 compiler (GCC, Clang,
Apple Clang, MSVC). It has zero non-libc dependencies.

### From this repo (for development)

```sh
git clone https://github.com/riboseinc/otlp-c
cd otlp-c
cmake -B build -G Ninja -DOTLP_C_BUILD_TESTS=ON -DOTLP_C_BUILD_EXAMPLES=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### Via vcpkg (for consumers)

In your project's `vcpkg.json`:

```json
{
  "dependencies": ["otlp-c"]
}
```

Then in your `CMakeLists.txt`:

```cmake
find_package(otlp-c CONFIG REQUIRED)
target_link_libraries(my-app PRIVATE otlp-c::otlp_c)
```

## 2. Build a minimal program

`minimal.c`:

```c
#include <otlp-c/otlp.h>

int main(void)
{
    otlp_exporter_opts_t opts = { .service_name = "demo" };
    otlp_exporter_t     *exp    = otlp_exporter_create(&opts);
    otlp_tracer_t       *tracer = otlp_tracer_create(
                                     "demo", "demo", "0.1.0");

    otlp_span_t *span = otlp_tracer_start_span(tracer, "hello");
    otlp_span_mark_end(span);
    otlp_exporter_emit(exp, span);
    otlp_span_free(span);

    otlp_exporter_flush(exp);
    otlp_tracer_free(tracer);
    otlp_exporter_free(exp);
    return 0;
}
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(demo C)
find_package(otlp-c CONFIG REQUIRED)
add_executable(demo minimal.c)
target_link_libraries(demo PRIVATE otlp-c::otlp_c)
```

## 3. Run a local collector + Jaeger

The library talks plain HTTP to `http://localhost:4318/v1/traces`
by default. Bring up the integration topology:

```sh
cd /path/to/otlp-c/tests/integration
docker compose up -d
cd -
```

This starts:
- `otelcol` on `:4318` (OTLP/HTTP receiver; forwards to Jaeger).
- `jaeger` on `:16686` (UI + query API).

## 4. Run your program

```sh
./demo
```

Then open <http://localhost:16686/search?service=demo> — the span
should appear within a second or two.

## 5. Teardown

```sh
cd /path/to/otlp-c/tests/integration && docker compose down
```

## What's next

### Metrics

```c
otlp_metric_t *m = otlp_metric_create(
    OTLP_METRIC_COUNTER, "requests_total", "1",
    "Total HTTP requests", NULL, 0);
otlp_metric_record(m, 1.0);
otlp_metric_mark_time(m);
otlp_metric_set_attribute_string(m, "method", "GET");
otlp_exporter_flush_metric(exp, m);  /* POSTs to /v1/metrics */
otlp_metric_free(m);
```

Supported types: `OTLP_METRIC_COUNTER`, `OTLP_METRIC_GAUGE`,
`OTLP_METRIC_HISTOGRAM`, `OTLP_METRIC_EXP_HISTOGRAM`.

### Logs

```c
otlp_log_record_t *lr = otlp_log_record_create(
    OTLP_SEVERITY_ERROR, "database connection failed");
otlp_log_record_mark_timestamp(lr);
otlp_log_record_set_attribute_string(lr, "db.host", "prod-db-1");
otlp_exporter_flush_log(exp, lr);  /* POSTs to /v1/logs */
otlp_log_record_free(lr);
```

### Context propagation

```c
/* Inject trace context into an HTTP request */
otlp_context_t ctx = otlp_context_from_span(span);
otlp_context_inject(ctx, my_header_set_fn, &request_headers);

/* Extract on the receiving side */
otlp_context_t parent = otlp_context_extract(
    my_header_get_fn, &incoming_headers);
```

### Sampling

```c
/* 50% deterministic sampling based on trace_id */
otlp_sampler_t *s = otlp_sampler_trace_id_ratio_based(0.5);
otlp_tracer_set_sampler(tracer, s);
```

### Custom allocator

```c
otlp_allocator_t my_alloc = {
    .alloc = my_malloc,
    .realloc = my_realloc,
    .free = my_free,
};
otlp_set_allocator(&my_alloc);

/* Or install a slab for hot-path small allocations */
otlp_install_slab_allocator(128, 256);
```

### Further reading

- [deployment.md](deployment.md) — production sidecar topology,
  TLS termination, Kubernetes DaemonSet patterns.
- [integration-test.md](integration-test.md) — how the integration
  test works, how to extend it.
- [API headers](../include/otlp-c/) — the public surface.
