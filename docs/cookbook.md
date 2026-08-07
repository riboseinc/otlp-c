# Cookbook

Working patterns for embedding `otlp-c` into different environments.
Complete, copy-paste-able examples.

## 1. No event loop (CLI tool, simple script)

The simplest case: emit a span, flush, exit. The exporter's
`flush()` blocks the calling thread until the batch is sent.

```c
#include <otlp-c/otlp.h>

int main(void)
{
    otlp_exporter_opts_t opts = { 0 };
    opts.service_name = "my-cli";

    otlp_exporter_t *exp = otlp_exporter_create(&opts);
    otlp_tracer_t   *tracer = otlp_tracer_create("my-cli", "my-tool", "1.0");

    otlp_span_t *span = otlp_tracer_start_span(tracer, "do-work");
    otlp_span_mark_end(span);

    /* otlp_exporter_emit_move() — fast path, takes ownership. */
    otlp_exporter_emit_move(exp, span);

    /* Block until the batch is sent (or retry budget exhausted). */
    otlp_exporter_flush(exp);

    otlp_tracer_free(tracer);
    otlp_exporter_free(exp);
    return 0;
}
```

## 2. Periodic tick from a timer (game loop, service main loop)

Most non-trivial apps have a main loop. Call `tick()` from the loop:

```c
/* In your main loop, once per frame / iteration: */
otlp_exporter_tick(exp, /*max_wait_ms=*/0);
```

`tick()` is non-blocking when the queue is empty and no in-flight
request is pending. Returns OTLP_OK on every call. No return value
to check.

For a "fire periodically" pattern:

```c
/* Every 100ms: */
static uint64_t last_tick = 0;
uint64_t now = mono_ms();
if (now - last_tick >= 100) {
    otlp_exporter_tick(exp, 0);
    last_tick = now;
}
```

## 3. Event-loop integration (libuv)

Expose the in-flight HTTP fd to your event loop. When it has work,
call `tick()`.

```c
static void on_poll_writable(uv_poll_t *h) {
    (void)h;
    otlp_exporter_tick(g_exp, 0);
}

void install_uv_handlers(uv_loop_t *loop) {
    otlp_poll_fd_t fds[2];
    size_t n = 0;
    otlp_exporter_poll_fds(g_exp, fds, 2, &n);
    for (size_t i = 0; i < n; i++) {
        uv_poll_t *h = malloc(sizeof(*h));
        uv_poll_init(loop, h, fds[i].fd);
        if (fds[i].events & 2) uv_poll_start(h, UV_WRITABLE, on_poll_writable);
        /* If also POLLIN, install a UV_READABLE handler. */
    }
}
```

Then your event loop does the polling; `tick()` only runs when there's
network activity.

## 4. Dedicated worker thread (caller-driven, not library-driven)

If you want the simplicity of `flush()` semantics but have a multi-threaded
app, spawn your own thread that calls `tick()` in a loop:

```c
static void *exporter_tick_thread(void *arg) {
    otlp_exporter_t *exp = arg;
    for (;;) {
        otlp_exporter_tick(exp, 100);
        /* Exit condition checked elsewhere. */
    }
    return NULL;
}

pthread_t tid;
pthread_create(&tid, NULL, exporter_tick_thread, exp);
```

Producer threads call `emit()` from anywhere. The tick thread drains
+ drives the HTTP state machine in the background.

## 5. Language-VM binding (Node, Python, Ruby)

Each VM has a thread/event-loop convention. The recommended pattern:

```c
/* In your N-API / Python C extension / Ruby C extension:

 * Expose:
 *   - otlp_exporter_t* → wrap as an opaque object
 *   - otlp_exporter_emit() → JS method that takes a span object
 *   - otlp_exporter_tick() → call from the VM's event loop hook
 *     (libuv for Node, asyncio for Python, EventMachine for Ruby)
 *   - otlp_exporter_flush() → GC finalizer
 */
```

The cleanest integration is to add timer callbacks via the VM's
event-loop binding. The library never spawns its own thread; the
VM owns lifecycle.

## 6. Embedded firmware (no event loop, no threads)

No-op the threading. Use the simplest pattern:

```c
/* In your main loop (or whenever a span is emitted): */
otlp_span_t *span = otlp_tracer_start_span(tracer, "sensor-read");
otlp_span_set_attribute_int(span, "sensor_id", 42);
otlp_span_mark_end(span);
otlp_exporter_emit_move(exp, span);
otlp_exporter_tick(exp, 0);  /* non-blocking */
```

If `tick()` doesn't fully send, the span sits in the queue until the
next iteration. On sensor-deactivate:

```c
otlp_exporter_flush(exp);
```

The library never assumes a thread exists; nothing breaks if the
loop is the only context.

## 7. Tracing firehose (high-throughput)

For paths that emit thousands of spans per second (libc preload,
per-syscall tracer, distributed-trace test driver):

```c
/* Pre-create span once, reuse across emits. */
otlp_span_t *tpl = otlp_tracer_start_span(tracer, "syscall");
otlp_span_set_attribute_string(tpl, "syscall.name", "open");
otlp_span_mark_end(tpl);
otlp_exporter_emit_move(exp, tpl);  /* ownership transferred */
```

Use `emit_move()` to skip the deep clone. Mark `batch_size` and
`queue_capacity` high enough to absorb bursts:

```c
otlp_exporter_opts_t opts = {
    .endpoint = "http://localhost:4318/v1/traces",
    .batch_size = 4096,
    .queue_capacity = 8192,
    .batch_ms = 100,
};
```

For maximum throughput, also run `otlp_exporter_tick(exp, 0)` from
your hot loop (it returns immediately when there's no I/O to do) or
mount the poll fds into your event loop.

## 8. Memory-bounded tracing (resource-constrained)

For environments where the queue must be small:

```c
otlp_exporter_opts_t opts = {
    .endpoint = "http://localhost:4318/v1/traces",
    .batch_size = 16,
    .queue_capacity = 64,  /* total in-flight memory ~ 16 * sizeof(span) */
    .max_retries = 1,
    .backoff_initial_ms = 100,
    .backoff_max_ms = 1000,
};
```

On overflow, `emit()` returns `OTLP_ERR_BUFFER_FULL` and the
`dropped_full` counter increments. Monitor that:

```c
otlp_exporter_stats_t stats;
otlp_exporter_get_stats(exp, &stats);
if (stats.dropped_full > 0) {
    /* circuit-break: emit less, or skip non-critical spans. */
}
```

The design is explicit: at small queue sizes, the library prefers
dropping over blocking. This matches the OpenTelemetry "drop
on overflow" semantic.

## 6. Emitting metrics

```c
/* Counter */
otlp_metric_t *m = otlp_metric_create(
    OTLP_METRIC_COUNTER, "http_requests_total", "1",
    "Total HTTP requests", NULL, 0);
otlp_metric_record(m, 1.0);
otlp_metric_mark_time(m);
otlp_metric_set_attribute_string(m, "method", "GET");
otlp_exporter_flush_metric(exp, m);
otlp_metric_free(m);

/* Histogram with explicit buckets */
double bounds[] = {0.005, 0.01, 0.05, 0.1, 0.5, 1.0, 5.0};
otlp_metric_t *h = otlp_metric_create(
    OTLP_METRIC_HISTOGRAM, "http_duration_seconds", "s",
    "Request duration", bounds, 7);
otlp_metric_record(h, 0.042);
otlp_metric_mark_time(h);
otlp_exporter_flush_metric(exp, h);
otlp_metric_free(h);
```

## 7. Emitting structured logs

```c
otlp_log_record_t *lr = otlp_log_record_create(
    OTLP_SEVERITY_ERROR, "database connection failed");
otlp_log_record_mark_timestamp(lr);
otlp_log_record_set_attribute_string(lr, "db.host", "prod-db-1");
otlp_log_record_set_attribute_int(lr, "retry_count", 3);

/* Correlate with a trace */
otlp_log_record_set_trace_id(lr, span_trace_id);
otlp_log_record_set_span_id(lr, span_span_id);

otlp_exporter_flush_log(exp, lr);
otlp_log_record_free(lr);
```

## 8. Propagating trace context across processes

```c
/* Sending side: inject context into HTTP headers */
static otlp_status_t
set_header(void *ctx, const char *key, const char *val)
{
    struct http_headers *h = ctx;
    return http_headers_set(h, key, val);
}

otlp_context_t ctx = otlp_context_from_span(span);
otlp_context_inject(ctx, set_header, &request_headers);

/* Receiving side: extract context from incoming headers */
static const char *
get_header(void *ctx, const char *key)
{
    struct http_headers *h = ctx;
    return http_headers_get(h, key);
}

otlp_context_t parent = otlp_context_extract(
    get_header, &incoming_headers);
if (parent.has_context) {
    /* Start child span inheriting the trace */
    child = otlp_tracer_start_child_span(tracer, "handle-request", ...);
}
```

## 9. Custom sampling

```c
/* 50% deterministic sampling (same trace_id always yields same decision) */
otlp_sampler_t *s = otlp_sampler_trace_id_ratio_based(0.5);
otlp_tracer_set_sampler(tracer, s);

/* Always-off (drop everything — useful for feature-flagging) */
otlp_tracer_set_sampler(tracer, otlp_sampler_always_off());
```

## 10. Custom allocator / slab

```c
/* Install a slab for hot-path small allocations */
otlp_install_slab_allocator(128, 256);
/* ... all subsequent otlp_malloc calls route through the slab ... */
otlp_uninstall_slab_allocator();

/* Or a fully custom allocator */
otlp_allocator_t my_alloc = {
    .alloc = my_pool_alloc,
    .realloc = my_pool_realloc,
    .free = my_pool_free,
};
otlp_set_allocator(&my_alloc);
```
