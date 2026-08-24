/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Event-loop integration example (the Phase 7 deliverable, finally
 * built in v0.5.105): drives the exporter from a poll() loop via
 * otlp_exporter_poll_fds() — THE embedding pattern for callers
 * whose main loop already owns I/O (games, servers, VMs).
 *
 * With no collector on the default endpoint the loop still exits
 * cleanly after the retry budget (the diagnostics events narrate
 * the retries); with one running, the span is sent.
 *
 *   cmake -B build -DOTLP_C_BUILD_EXAMPLES=ON
 *   cmake --build build
 *   ./build/examples/otlp_example_event_loop
 */
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <otlp-c/otlp.h>

#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static unsigned g_sent;
static unsigned g_dropped;
static unsigned g_retries;

static void
on_event(void *ctx, const otlp_event_t *ev)
{
	(void) ctx;
	switch (ev->code)
	{
		case OTLP_EVT_BATCH_SENT:
			g_sent++;
			break;
		case OTLP_EVT_ITEMS_DROPPED:
			g_dropped += (unsigned) ev->count;
			break;
		case OTLP_EVT_RETRY_ARMED:
			g_retries++;
			break;
		default:
			break;
	}
}

static uint64_t
mono_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000ULL +
		(uint64_t) ts.tv_nsec / 1000000ULL;
}

int
main(void)
{
	otlp_exporter_opts_t opts = { 0 };
	otlp_exporter_t *exp;
	otlp_tracer_t *tracer;
	otlp_span_t *span;
	otlp_poll_fd_t fds[1];
	otlp_exporter_stats_t stats;
	uint64_t deadline;

	opts.service_name = "event-loop-demo";
	opts.batch_ms = 50;
	opts.max_retries = 1;
	exp = otlp_exporter_create(&opts);
	if (!exp)
	{
		fprintf(stderr, "exporter create failed\n");
		return 1;
	}
	otlp_exporter_set_event_logger(exp, on_event, NULL);

	tracer = otlp_tracer_create("event-loop-demo", "demo", otlp_version());
	if (!tracer)
	{
		otlp_exporter_free(exp);
		return 1;
	}
	span = otlp_tracer_start_span(tracer, "demo-op");
	if (span)
	{
		otlp_span_set_attribute_string(span, "demo", "event-loop");
		otlp_span_mark_end(span);
		otlp_exporter_emit(exp, span);
		otlp_span_free(span);
	}

	/* The event loop: sleep on the exposed fds while a request is
	 * in flight, tick on wake (or every batch_ms otherwise). */
	deadline = mono_ms() + 10000;
	while (mono_ms() < deadline)
	{
		otlp_exporter_stats_t st_stats;
		size_t n = 0;

		otlp_exporter_tick(exp, 0);
		if (otlp_exporter_poll_fds(exp, fds, 1, &n) != OTLP_OK)
			break;
		if (n > 0)
		{
			struct pollfd pfd;

			pfd.fd = fds[0].fd;
			pfd.events = (short) fds[0].events;
			pfd.revents = 0;
			poll(&pfd, 1, 50);
		}
		else
		{
			/* Nothing in flight: wait out the batch timer.
			 * poll with no fds is a portable sleep. */
			poll(NULL, 0, 10);
		}
		otlp_exporter_get_stats(exp, &st_stats);
		if (st_stats.sent > 0 || st_stats.dropped_err > 0)
			break;
	}

	otlp_exporter_flush(exp);
	otlp_exporter_get_stats(exp, &stats);
	printf("otlp-c %s event-loop demo: sent=%llu dropped=%llu "
	       "(events: sent=%u dropped=%u retries=%u)\n",
		otlp_version(),
		(unsigned long long) stats.sent,
		(unsigned long long) stats.dropped_err,
		g_sent,
		g_dropped,
		g_retries);
	otlp_tracer_free(tracer);
	otlp_exporter_free(exp);
	return 0;
}
