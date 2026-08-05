/* SPDX-License-Identifier: Apache-2.0 */
/*
 * HTTP/1.1 POST state machine. See src/http_client.h for the API.
 *
 * Wire format produced (POST, plain HTTP, Connection: close):
 *
 *   POST <path> HTTP/1.1\r\n
 *   Host: <host>\r\n
 *   User-Agent: <ua>\r\n
 *   Content-Type: application/x-protobuf\r\n
 *   Content-Length: <n>\r\n
 *   Connection: close\r\n
 *   \r\n
 *   <body>
 *
 * Response parsing: minimal. We scan for "HTTP/1.1 NNN" status line
 * and a Content-Length header; the body is everything after \r\n\r\n
 * up to either Content-Length bytes or EOF. No chunked encoding
 * (collector responses are short, Connection: close).
 */
#include "http_client.h"
#include "platform.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── URL parser ───────────────────────────────────────────────── */

static int
parse_uint16(const char *s, size_t len, uint16_t *out)
{
	uint32_t v = 0;
	size_t i;

	for (i = 0; i < len; i++)
	{
		if (s[i] < '0' || s[i] > '9')
			return -1;
		v = v * 10u + (uint32_t) (s[i] - '0');
		if (v > 65535u)
			return -1;
	}
	if (i == 0)
		return -1;
	*out = (uint16_t) v;
	return 0;
}

otlp_status_t
otlp_http_parse_url(const char *url, struct otlp_http_url *out)
{
	const char *scheme_end;
	const char *host_start;
	const char *host_end;
	const char *p;
	size_t host_len;
	size_t path_len;

	if (!url || !out)
		return OTLP_ERR_NULL;

	/* Accept only http://. */
	if (strncmp(url, "http://", 7) != 0)
		return OTLP_ERR_INVALID_ARGUMENT;
	scheme_end = url + 7;
	if (*scheme_end == '\0' || *scheme_end == '/')
		return OTLP_ERR_INVALID_ARGUMENT;

	/* Find end of host (':', '/', or '\0'). */
	host_start = scheme_end;
	host_end = host_start;
	while (*host_end != '\0' && *host_end != ':' && *host_end != '/')
		host_end++;
	host_len = (size_t) (host_end - host_start);
	if (host_len == 0 || host_len >= OTLP_HTTP_HOST_MAX)
		return OTLP_ERR_INVALID_ARGUMENT;
	memcpy(out->host, host_start, host_len);
	out->host[host_len] = '\0';

	/* Optional port. */
	if (*host_end == ':')
	{
		const char *port_start = host_end + 1;
		const char *port_end = port_start;

		while (*port_end != '\0' && *port_end != '/')
			port_end++;
		if (parse_uint16(port_start,
			    (size_t) (port_end - port_start),
			    &out->port) != 0)
			return OTLP_ERR_INVALID_ARGUMENT;
		host_end = port_end;
	}
	else
	{
		out->port = 80;
	}

	/* Path: rest of the URL (may be empty → "/"). */
	p = host_end;
	if (*p == '\0')
	{
		out->path[0] = '/';
		out->path[1] = '\0';
	}
	else
	{
		path_len = strlen(p);
		if (path_len >= OTLP_HTTP_PATH_MAX)
			return OTLP_ERR_INVALID_ARGUMENT;
		memcpy(out->path, p, path_len + 1);
	}
	return OTLP_OK;
}

/* ── Request state ────────────────────────────────────────────── */

#define OTLP_HTTP_RESP_MAX (64 * 1024) /* collector bodies are small */

struct otlp_http_request
{
	otlp_http_req_state_t state;
	struct otlp_socket *sock;

	/* Encoded request bytes (header + body). */
	uint8_t *req_buf;
	size_t req_len;
	size_t req_sent;

	/* Response accumulation. */
	uint8_t *resp_buf;
	size_t resp_cap;
	size_t resp_len;

	/* Parsed response. */
	int http_status;
	const uint8_t *body_ptr; /* into resp_buf */
	size_t body_len;
};

static otlp_status_t
build_request(struct otlp_http_request *r,
	const struct otlp_http_url *url,
	const char *user_agent,
	const uint8_t *body,
	size_t body_len)
{
	char head[1024];
	int n;
	size_t total;

	/* Content-Length is always present; we don't chunk. */
	n = snprintf(head,
		sizeof(head),
		"POST %s HTTP/1.1\r\n"
		"Host: %s\r\n"
		"User-Agent: %s\r\n"
		"Content-Type: application/x-protobuf\r\n"
		"Content-Length: %zu\r\n"
		"Connection: close\r\n"
		"\r\n",
		url->path,
		url->host,
		user_agent ? user_agent : "otlp-c",
		body_len);
	if (n < 0 || (size_t) n >= sizeof(head))
		return OTLP_ERR_OVERFLOW;

	total = (size_t) n + body_len;
	if (total < body_len) /* overflow check */
		return OTLP_ERR_OVERFLOW;

	r->req_buf = malloc(total);
	if (!r->req_buf)
		return OTLP_ERR_NOMEM;
	memcpy(r->req_buf, head, (size_t) n);
	if (body_len > 0)
		memcpy(r->req_buf + (size_t) n, body, body_len);
	r->req_len = total;
	r->req_sent = 0;
	return OTLP_OK;
}

otlp_status_t
otlp_http_request_start(otlp_http_request_t **out,
	const struct otlp_http_url *url,
	const char *user_agent,
	const uint8_t *body,
	size_t body_len)
{
	struct otlp_http_request *r;
	otlp_status_t st;

	if (!out || !url)
		return OTLP_ERR_NULL;
	if (body_len > 0 && !body)
		return OTLP_ERR_NULL;

	r = calloc(1, sizeof(*r));
	if (!r)
		return OTLP_ERR_NOMEM;
	r->state = OTLP_HTTP_REQ_CONNECTING;

	st = build_request(r, url, user_agent, body, body_len);
	if (st != OTLP_OK)
		goto fail;

	st = otlp_socket_connect(&r->sock, url->host, url->port);
	if (st != OTLP_OK)
		goto fail;

	/* If connect() completed synchronously (rare for non-blocking
	 * on the first call), advance state to SENDING. */
	st = otlp_socket_finish_connect(r->sock);
	if (st == OTLP_OK)
		r->state = OTLP_HTTP_REQ_SENDING;
	else if (st == OTLP_ERR_WOULDBLOCK)
		(void) st; /* leave in CONNECTING */
	else
		goto fail;

	*out = r;
	return OTLP_OK;

fail:
	if (r->sock)
		otlp_socket_close(r->sock);
	free(r->req_buf);
	free(r);
	return st;
}

/* ── Response parser ──────────────────────────────────────────── */

static int
find_substring(const uint8_t *hay,
	size_t hay_len,
	const char *needle,
	size_t needle_len)
{
	size_t i;

	if (hay_len < needle_len)
		return -1;
	for (i = 0; i <= hay_len - needle_len; i++)
	{
		if (memcmp(hay + i, needle, needle_len) == 0)
			return (int) i;
	}
	return -1;
}

/* Try to parse a complete response from r->resp_buf. Returns:
 *   1  — complete; http_status and body filled in.
 *   0  — incomplete; need more data.
 *  -1  — malformed. */
static int
try_parse_response(struct otlp_http_request *r)
{
	int hdr_end_off;
	const char *body_start;
	size_t body_off;
	const char *cl;
	const char *p;
	long content_length = -1;

	hdr_end_off = find_substring(r->resp_buf, r->resp_len, "\r\n\r\n", 4);
	if (hdr_end_off < 0)
		return 0;
	body_off = (size_t) hdr_end_off + 4;
	body_start = (const char *) r->resp_buf + body_off;

	/* Parse status line: "HTTP/1.1 NNN <reason>\r\n". */
	if (r->resp_len < 12 || memcmp(r->resp_buf, "HTTP/", 5) != 0)
		return -1;
	/* Find first space, then the 3-digit status. */
	p = (const char *) r->resp_buf + 5;
	while (p < body_start && *p != ' ')
		p++;
	if (p >= body_start || *p != ' ')
		return -1;
	p++; /* skip space */
	if (p + 3 > body_start || !isdigit((unsigned char) p[0]) ||
		!isdigit((unsigned char) p[1]) ||
		!isdigit((unsigned char) p[2]))
		return -1;
	r->http_status = (p[0] - '0') * 100 + (p[1] - '0') * 10 + (p[2] - '0');

	/* Find Content-Length (case-insensitive). */
	for (cl = (const char *) r->resp_buf; cl < body_start - 2; cl++)
	{
		if (strncasecmp(cl, "Content-Length:", 15) == 0)
		{
			cl += 15;
			while (cl < body_start && *cl == ' ')
				cl++;
			content_length = 0;
			while (cl < body_start && isdigit((unsigned char) *cl))
			{
				content_length =
					content_length * 10 + (*cl - '0');
				if (content_length > OTLP_HTTP_RESP_MAX)
					return -1;
				cl++;
			}
			break;
		}
	}

	if (content_length >= 0)
	{
		/* Need exactly content_length bytes after \r\n\r\n. */
		if (r->resp_len - body_off < (size_t) content_length)
			return 0;
		r->body_ptr = (const uint8_t *) body_start;
		r->body_len = (size_t) content_length;
	}
	else
	{
		/* No Content-Length; consume until EOF. The caller's
		 * read loop detects EOF and calls us here one last time. */
		r->body_ptr = (const uint8_t *) body_start;
		r->body_len = r->resp_len - body_off;
	}
	return 1;
}

/* ── Step ─────────────────────────────────────────────────────── */

static otlp_status_t
step_connecting(struct otlp_http_request *r)
{
	otlp_status_t st = otlp_socket_finish_connect(r->sock);

	if (st == OTLP_OK)
	{
		r->state = OTLP_HTTP_REQ_SENDING;
		return OTLP_OK;
	}
	return st; /* WOULDBLOCK or error */
}

static otlp_status_t
step_sending(struct otlp_http_request *r)
{
	size_t n_written;
	otlp_status_t st;

	st = otlp_socket_write(r->sock,
		r->req_buf + r->req_sent,
		r->req_len - r->req_sent,
		&n_written);
	if (st != OTLP_OK)
		return st;
	r->req_sent += n_written;
	if (r->req_sent == r->req_len)
	{
		r->state = OTLP_HTTP_REQ_READING;
		/* Allocate response buffer now. */
		r->resp_cap = 4096;
		r->resp_buf = malloc(r->resp_cap);
		if (!r->resp_buf)
			return OTLP_ERR_NOMEM;
		r->resp_len = 0;
	}
	return OTLP_OK;
}

static otlp_status_t
step_reading(struct otlp_http_request *r)
{
	uint8_t small[4096];
	size_t n_read;
	otlp_status_t st;
	int parsed;

	st = otlp_socket_read(r->sock, small, sizeof(small), &n_read);
	if (st == OTLP_ERR_WOULDBLOCK)
		return OTLP_ERR_WOULDBLOCK;
	if (st != OTLP_OK)
		return st;

	if (n_read > 0)
	{
		/* Grow if needed. */
		if (r->resp_len + n_read > r->resp_cap)
		{
			size_t new_cap = r->resp_cap;
			uint8_t *p;

			while (new_cap < r->resp_len + n_read)
			{
				if (new_cap > OTLP_HTTP_RESP_MAX)
					return OTLP_ERR_OVERFLOW;
				new_cap *= 2;
			}
			p = realloc(r->resp_buf, new_cap);
			if (!p)
				return OTLP_ERR_NOMEM;
			r->resp_buf = p;
			r->resp_cap = new_cap;
		}
		memcpy(r->resp_buf + r->resp_len, small, n_read);
		r->resp_len += n_read;
	}

	parsed = try_parse_response(r);
	if (parsed < 0)
		return OTLP_ERR_INVALID_RESPONSE;
	if (parsed == 1)
	{
		r->state = OTLP_HTTP_REQ_DONE;
		return OTLP_OK;
	}
	/* parsed == 0: need more data. */
	if (otlp_socket_eof(r->sock))
	{
		/* Peer closed without sending a complete response.
		 * Try one final parse — maybe it was complete and
		 * there was just no Content-Length. */
		if (try_parse_response(r) == 1)
		{
			r->state = OTLP_HTTP_REQ_DONE;
			return OTLP_OK;
		}
		return OTLP_ERR_INVALID_RESPONSE;
	}
	return OTLP_OK; /* try _step again later */
}

otlp_status_t
otlp_http_request_step(otlp_http_request_t *r)
{
	otlp_status_t st;

	if (!r)
		return OTLP_ERR_NULL;
	switch (r->state)
	{
		case OTLP_HTTP_REQ_CONNECTING:
			st = step_connecting(r);
			break;
		case OTLP_HTTP_REQ_SENDING:
			st = step_sending(r);
			break;
		case OTLP_HTTP_REQ_READING:
			st = step_reading(r);
			break;
		default:
			return OTLP_OK; /* terminal: no-op */
	}
	if (st != OTLP_OK && st != OTLP_ERR_WOULDBLOCK)
		r->state = OTLP_HTTP_REQ_FAILED;
	return st;
}

otlp_http_req_state_t
otlp_http_request_state(const otlp_http_request_t *r)
{
	return r ? r->state : OTLP_HTTP_REQ_FAILED;
}

int
otlp_http_request_fd(const otlp_http_request_t *r)
{
	return r && r->sock ? otlp_socket_fd(r->sock) : -1;
}

int
otlp_http_request_events(const otlp_http_request_t *r)
{
	if (!r)
		return 0;
	switch (r->state)
	{
		case OTLP_HTTP_REQ_CONNECTING:
		case OTLP_HTTP_REQ_SENDING:
			return 2; /* POLLOUT */
		case OTLP_HTTP_REQ_READING:
			return 1; /* POLLIN */
		default:
			return 0;
	}
}

int
otlp_http_request_http_status(const otlp_http_request_t *r)
{
	return r ? r->http_status : 0;
}

const uint8_t *
otlp_http_request_body(const otlp_http_request_t *r, size_t *len_out)
{
	if (len_out)
		*len_out = r ? r->body_len : 0;
	return r ? r->body_ptr : NULL;
}

void
otlp_http_request_free(otlp_http_request_t *r)
{
	if (!r)
		return;
	if (r->sock)
		otlp_socket_close(r->sock);
	free(r->req_buf);
	free(r->resp_buf);
	free(r);
}
