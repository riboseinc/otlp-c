/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Hand-rolled protobuf decoder, for property tests that need to
 * round-trip encoder output. Test-only; lives under tests/property
 * so multiple property files share the same implementation.
 *
 * Mirrors the encoder's wire types exactly:
 *   0 = varint
 *   1 = fixed64
 *   2 = length-delimited
 *   5 = fixed32
 */
#ifndef OTLP_C_TEST_PROPERTY_DECODER_H
#define OTLP_C_TEST_PROPERTY_DECODER_H

#include <otlp-c/status.h>

#include <stddef.h>
#include <stdint.h>

/* Decode a varint at *pos in buf. Advances *pos. */
static inline otlp_status_t
decode_varint(const uint8_t *data, size_t len, size_t *pos, uint64_t *out)
{
	uint64_t	v = 0;
	int		shift = 0;

	while (*pos < len) {
		uint8_t b = data[(*pos)++];

		v |= (uint64_t)(b & 0x7F) << shift;
		if ((b & 0x80) == 0) {
			*out = v;
			return OTLP_OK;
		}
		shift += 7;
		if (shift > 63)
			return OTLP_ERR_PROTOCOL;
	}
	return OTLP_ERR_PROTOCOL;
}

/* Decode the next field tag at *pos. Returns OTLP_OK and advances
 * *pos, writes field_num + wire_type. */
static inline otlp_status_t
decode_tag(const uint8_t *data, size_t len, size_t *pos,
	   uint32_t *field_num, int *wire_type)
{
	uint64_t	key;
	otlp_status_t	st;

	st = decode_varint(data, len, pos, &key);
	if (st != OTLP_OK)
		return st;
	*field_num = (uint32_t)(key >> 3);
	*wire_type = (int)(key & 0x7);
	return OTLP_OK;
}

/* Skip a value of the given wire type at *pos. */
static inline otlp_status_t
skip_value(const uint8_t *data, size_t len, size_t *pos, int wire_type)
{
	uint64_t	v;
	size_t		field_len;

	switch (wire_type) {
	case 0:
		return decode_varint(data, len, pos, &v);
	case 1:
		if (*pos + 8 > len)
			return OTLP_ERR_PROTOCOL;
		*pos += 8;
		return OTLP_OK;
	case 2: {
		otlp_status_t st = decode_varint(data, len, pos, &v);
		if (st != OTLP_OK)
			return st;
		field_len = (size_t)v;
		if (*pos + field_len > len)
			return OTLP_ERR_PROTOCOL;
		*pos += field_len;
		return OTLP_OK;
	}
	case 5:
		if (*pos + 4 > len)
			return OTLP_ERR_PROTOCOL;
		*pos += 4;
		return OTLP_OK;
	default:
		return OTLP_ERR_PROTOCOL;
	}
}

#endif
