/* SPDX-License-Identifier: Apache-2.0 */
/*
 * HTTP/1.1 client — STUB.
 *
 * Phase 3 of the roadmap. Real implementation will:
 *
 *   1. Parse the endpoint URL (scheme, host, port, path).
 *   2. Resolve the host via getaddrinfo.
 *   3. Open a TCP socket (POSIX) or WSASocket (Win32).
 *   4. Write a minimal HTTP/1.1 POST request:
 *        POST <path> HTTP/1.1
 *        Host: <host>
 *        Content-Type: application/x-protobuf
 *        Content-Length: <N>
 *        User-Agent: <user-agent>
 *        <empty line>
 *        <body>
 *   5. Read the response line + headers + body.
 *
 * Connection pooling is P1. v0.1.0 connects per request.
 *
 * TLS is not implemented. Production users should run an otelcol
 * on localhost for TLS termination to the backend.
 */
#include "platform.h"
#include <otlp-c/status.h>

#include <stddef.h>
#include <string.h>

/* TODO Phase 3. */
