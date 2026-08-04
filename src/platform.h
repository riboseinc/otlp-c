/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Cross-platform helpers — STUB.
 *
 * Phase 3 of the roadmap. Real implementation will provide:
 *
 *   - otlp_platform_socket_create / close
 *   - otlp_platform_connect(host, port, timeout_ms)
 *   - otlp_platform_write(fd, data, len, timeout_ms)
 *   - otlp_platform_read(fd, data, cap, timeout_ms)
 *   - otlp_platform_now_unix_nano()        (UTC wall clock)
 *   - otlp_platform_now_mono_nano()        (monotonic)
 *   - otlp_platform_mutex_init / lock / unlock / destroy
 *   - otlp_platform_thread_create / join
 *
 * Two source files implement these:
 *   platform_unix.c — POSIX (Linux, macOS, BSDs)
 *   platform_win.c  — Win32
 *
 * CMake picks the right source per platform.
 */
#ifndef OTLP_C_PLATFORM_H
#define OTLP_C_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include <otlp-c/status.h>

/* Wall-clock nanoseconds since Unix epoch (UTC). */
otlp_status_t otlp_platform_now_unix_nano(uint64_t *out);

/* Monotonic nanoseconds, undefined epoch. */
otlp_status_t otlp_platform_now_mono_nano(uint64_t *out);

#endif
