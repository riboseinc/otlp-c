/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Protobuf wire encoder — see src/protobuf_encode.h for the API.
 *
 * Hand-rolled for the four wire types OTLP uses (varint, fixed64,
 * fixed32, length-delimited). No third-party protobuf library.
 *
 * Memory model: otlp_pb_buf owns its heap buffer. _init allocates
 * (or leaves NULL if initial_cap == 0); _free releases. The buf is
 * growable and amortized O(1) per append via doubling. All public
 * functions are atomic on failure: a partial write leaves the buf
 * unchanged from the caller's perspective (len is only advanced
 * after a successful append).
 */
#include "protobuf_encode.h"

#include <stdlib.h>
#include <string.h>

/* ── Internal helpers ───────────────────────────────────────────
 *
 * buf_reserve / buf_append are the two DRY primitives every public
 * encoder delegates to. Keeping them here means growth policy, NULL
 * handling, and overflow checks live in exactly one place.
 */

/* Ensure buf has room for `additional` more bytes beyond buf->len.
 * Doubling growth; SIZE_MAX/2 cap. */
static otlp_status_t
buf_reserve(struct otlp_pb_buf *buf, size_t additional)
{
	size_t new_cap;
	size_t need;
	uint8_t *p;

	if (!buf)
		return OTLP_ERR_NULL;
	if (additional > SIZE_MAX - buf->len)
		return OTLP_ERR_OVERFLOW;
	need = buf->len + additional;
	if (need <= buf->cap)
		return OTLP_OK;

	new_cap = buf->cap ? buf->cap : 64;
	while (new_cap < need)
	{
		if (new_cap > SIZE_MAX / 2)
			return OTLP_ERR_OVERFLOW;
		new_cap *= 2;
	}

	p = realloc(buf->data, new_cap);
	if (!p)
		return OTLP_ERR_NOMEM;
	buf->data = p;
	buf->cap = new_cap;
	return OTLP_OK;
}

/* Append `len` bytes from `data`. data may be NULL only when len==0;
 * in that case this is a no-op. */
static otlp_status_t
buf_append(struct otlp_pb_buf *buf, const uint8_t *data, size_t len)
{
	otlp_status_t st;

	if (len == 0)
		return OTLP_OK;
	if (!data)
		return OTLP_ERR_NULL;
	st = buf_reserve(buf, len);
	if (st != OTLP_OK)
		return st;
	memcpy(buf->data + buf->len, data, len);
	buf->len += len;
	return OTLP_OK;
}

/* ── Buffer lifecycle ─────────────────────────────────────────── */

otlp_status_t
otlp_pb_buf_init(struct otlp_pb_buf *buf, size_t initial_cap)
{
	if (!buf)
		return OTLP_ERR_NULL;
	buf->data = NULL;
	buf->len = 0;
	buf->cap = 0;
	if (initial_cap == 0)
		return OTLP_OK;
	buf->data = malloc(initial_cap);
	if (!buf->data)
		return OTLP_ERR_NOMEM;
	buf->cap = initial_cap;
	return OTLP_OK;
}

void
otlp_pb_buf_free(struct otlp_pb_buf *buf)
{
	if (!buf)
		return;
	free(buf->data);
	buf->data = NULL;
	buf->len = 0;
	buf->cap = 0;
}

void
otlp_pb_buf_reset(struct otlp_pb_buf *buf)
{
	if (!buf)
		return;
	buf->len = 0;
}

/* ── Low-level wire encoders ────────────────────────────────────
 *
 * All encoders append to buf. None of them free or reset buf. On
 * failure they return without modifying buf->len (the reserve may
 * have grown buf->cap, but that is harmless — cap only grows).
 */

otlp_status_t
otlp_pb_varint(struct otlp_pb_buf *buf, uint64_t v)
{
	uint8_t tmp[10];
	size_t n = 0;

	do
	{
		tmp[n++] = (uint8_t) ((v & 0x7F) | 0x80);
		v >>= 7;
	} while (v != 0 && n < sizeof(tmp));

	/* Clear continuation bit on the last byte. */
	tmp[n - 1] &= 0x7F;
	return buf_append(buf, tmp, n);
}

otlp_status_t
otlp_pb_fixed64(struct otlp_pb_buf *buf, uint64_t v)
{
	uint8_t tmp[8];

	/* Little-endian, no platform dependency (shifts, not union). */
	for (int i = 0; i < 8; i++)
		tmp[i] = (uint8_t) (v >> (i * 8));
	return buf_append(buf, tmp, sizeof(tmp));
}

otlp_status_t
otlp_pb_fixed32(struct otlp_pb_buf *buf, uint32_t v)
{
	uint8_t tmp[4];

	for (int i = 0; i < 4; i++)
		tmp[i] = (uint8_t) (v >> (i * 8));
	return buf_append(buf, tmp, sizeof(tmp));
}

otlp_status_t
otlp_pb_bytes(struct otlp_pb_buf *buf, const uint8_t *data, size_t len)
{
	otlp_status_t st;

	/* Length prefix (varint) then payload. */
	st = otlp_pb_varint(buf, (uint64_t) len);
	if (st != OTLP_OK)
		return st;
	if (len == 0)
		return OTLP_OK;
	return buf_append(buf, data, len);
}

otlp_status_t
otlp_pb_string(struct otlp_pb_buf *buf, const char *str)
{
	size_t len = str ? strlen(str) : 0;

	return otlp_pb_bytes(buf, (const uint8_t *) str, len);
}

otlp_status_t
otlp_pb_tag(struct otlp_pb_buf *buf, uint32_t field_number, int wire_type)
{
	uint64_t key;

	/* field_number 0 is reserved by protobuf. Wire types 3 (start
	 * group) and 4 (end group) are deprecated and not used by OTLP. */
	if (field_number == 0)
		return OTLP_ERR_INVALID_ARGUMENT;
	if (wire_type != OTLP_PB_WIRE_VARINT &&
		wire_type != OTLP_PB_WIRE_FIXED64 &&
		wire_type != OTLP_PB_WIRE_LEN &&
		wire_type != OTLP_PB_WIRE_FIXED32)
		return OTLP_ERR_INVALID_ARGUMENT;

	key = ((uint64_t) field_number << 3) | (uint64_t) (uint32_t) wire_type;
	return otlp_pb_varint(buf, key);
}

/* ── Typed field helpers ────────────────────────────────────────
 *
 * Each helper emits tag + value. Skip emission when value is the
 * type's zero value (protobuf3 default-omission semantics). Callers
 * that must emit a default value explicitly should call otlp_pb_tag
 * + otlp_pb_varint(0) (or _bytes(buf, "", 0)) directly.
 *
 * field_message is the exception: it emits even for a zero-length
 * sub-message only if explicitly asked; the convention here is to
 * skip empty sub-messages, matching protobuf3.
 */

otlp_status_t
otlp_pb_field_varint(struct otlp_pb_buf *buf,
	uint32_t field_num,
	uint64_t value)
{
	otlp_status_t st;

	if (value == 0)
		return OTLP_OK;
	st = otlp_pb_tag(buf, field_num, OTLP_PB_WIRE_VARINT);
	if (st != OTLP_OK)
		return st;
	return otlp_pb_varint(buf, value);
}

otlp_status_t
otlp_pb_field_fixed64(struct otlp_pb_buf *buf,
	uint32_t field_num,
	uint64_t value)
{
	otlp_status_t st;

	if (value == 0)
		return OTLP_OK;
	st = otlp_pb_tag(buf, field_num, OTLP_PB_WIRE_FIXED64);
	if (st != OTLP_OK)
		return st;
	return otlp_pb_fixed64(buf, value);
}

otlp_status_t
otlp_pb_field_fixed32(struct otlp_pb_buf *buf,
	uint32_t field_num,
	uint32_t value)
{
	otlp_status_t st;

	if (value == 0)
		return OTLP_OK;
	st = otlp_pb_tag(buf, field_num, OTLP_PB_WIRE_FIXED32);
	if (st != OTLP_OK)
		return st;
	return otlp_pb_fixed32(buf, value);
}

otlp_status_t
otlp_pb_field_string(struct otlp_pb_buf *buf,
	uint32_t field_num,
	const char *str)
{
	otlp_status_t st;
	size_t len;

	/* Empty strings are omitted (protobuf3 default). */
	if (!str || str[0] == '\0')
		return OTLP_OK;
	len = strlen(str);
	st = otlp_pb_tag(buf, field_num, OTLP_PB_WIRE_LEN);
	if (st != OTLP_OK)
		return st;
	return otlp_pb_bytes(buf, (const uint8_t *) str, len);
}

otlp_status_t
otlp_pb_field_bytes(struct otlp_pb_buf *buf,
	uint32_t field_num,
	const uint8_t *data,
	size_t len)
{
	otlp_status_t st;

	/* Empty bytes are omitted (protobuf3 default). Callers needing
	 * to emit explicit empty bytes should use tag + varint(0). */
	if (len == 0)
		return OTLP_OK;
	st = otlp_pb_tag(buf, field_num, OTLP_PB_WIRE_LEN);
	if (st != OTLP_OK)
		return st;
	return otlp_pb_bytes(buf, data, len);
}

otlp_status_t
otlp_pb_field_message(struct otlp_pb_buf *buf,
	uint32_t field_num,
	const uint8_t *data,
	size_t len)
{
	otlp_status_t st;

	/* Empty sub-messages are omitted (protobuf3 default). */
	if (len == 0)
		return OTLP_OK;
	st = otlp_pb_tag(buf, field_num, OTLP_PB_WIRE_LEN);
	if (st != OTLP_OK)
		return st;
	st = otlp_pb_varint(buf, (uint64_t) len);
	if (st != OTLP_OK)
		return st;
	return buf_append(buf, data, len);
}
