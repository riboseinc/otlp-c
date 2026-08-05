/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property tests for the varint wire encoder.
 *
 *   prop_varint_roundtrip  — any uint64_t encodes then decodes back.
 *   prop_varint_size       — encoded length matches ceil(bits/7).
 *   prop_varint_extremes   — 0 and UINT64_MAX produce specific bytes.
 *   prop_varint_field_zero — field_varint with value 0 emits nothing.
 *
 * The decoder under test is hand-rolled here (not the production
 * decoder, which doesn't exist yet — otlp-c only encodes). This
 * file is therefore the canonical reference for how varints decode.
 */
#include "prng.h"
#include "property_harness.h"

#include "../src/protobuf_encode.h"

#include <stdint.h>

/* ── Hand-rolled decoder ──────────────────────────────────────── */

static otlp_status_t
decode_varint(const uint8_t *data, size_t len, size_t *pos, uint64_t *out)
{
	uint64_t v = 0;
	int shift = 0;

	while (*pos < len)
	{
		uint8_t b = data[(*pos)++];

		v |= (uint64_t) (b & 0x7F) << shift;
		if ((b & 0x80) == 0)
		{
			*out = v;
			return OTLP_OK;
		}
		shift += 7;
		if (shift > 63)
			return OTLP_ERR_PROTOCOL; /* malformed */
	}
	return OTLP_ERR_PROTOCOL; /* ran off end without terminator */
}

/* ── Properties ───────────────────────────────────────────────── */

static int
prop_varint_roundtrip(uint64_t seed)
{
	struct prng p;
	struct otlp_pb_buf buf;
	uint64_t value;
	uint64_t decoded;
	size_t pos = 0;
	int ok = 0;

	prng_seed(&p, seed);
	value = prng_next(&p);

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	if (otlp_pb_varint(&buf, value) != OTLP_OK)
		goto out;
	if (decode_varint(buf.data, buf.len, &pos, &decoded) != OTLP_OK)
		goto out;
	ok = (decoded == value);

out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_varint_size(uint64_t seed)
{
	struct prng p;
	struct otlp_pb_buf buf;
	uint64_t value;
	size_t expected;
	int ok = 0;

	prng_seed(&p, seed);
	value = prng_next(&p);

	/* Byte length = ceil(bits_needed / 7), minimum 1. */
	if (value == 0)
	{
		expected = 1;
	}
	else
	{
		int bits = 0;
		uint64_t v = value;

		while (v)
		{
			bits++;
			v >>= 1;
		}
		expected = (size_t) ((bits + 6) / 7);
	}

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	if (otlp_pb_varint(&buf, value) != OTLP_OK)
		goto out;
	ok = (buf.len == expected);

out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_varint_extremes(uint64_t seed)
{
	struct otlp_pb_buf buf;
	int ok = 0;

	(void) seed;

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;

	/* 0 → single byte 0x00 */
	otlp_pb_buf_reset(&buf);
	if (otlp_pb_varint(&buf, 0) != OTLP_OK)
		goto out;
	if (buf.len != 1 || buf.data[0] != 0x00)
		goto out;

	/* UINT64_MAX → 10 bytes: first 9 are 0xFF (7 bits + continuation),
	 * last byte is 0x01 (high bit of value, no continuation). */
	otlp_pb_buf_reset(&buf);
	if (otlp_pb_varint(&buf, UINT64_MAX) != OTLP_OK)
		goto out;
	if (buf.len != 10)
		goto out;
	for (int i = 0; i < 9; i++)
		if (buf.data[i] != 0xFF)
			goto out;
	if (buf.data[9] != 0x01)
		goto out;

	/* 128 → 2 bytes: 0x80 0x01 */
	otlp_pb_buf_reset(&buf);
	if (otlp_pb_varint(&buf, 128) != OTLP_OK)
		goto out;
	if (buf.len != 2 || buf.data[0] != 0x80 || buf.data[1] != 0x01)
		goto out;

	ok = 1;

out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_varint_field_zero_omitted(uint64_t seed)
{
	struct otlp_pb_buf buf;
	int ok = 0;

	(void) seed;

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	if (otlp_pb_field_varint(&buf, 7, 0) != OTLP_OK)
		goto out;
	/* Default value → field omitted → empty buf. */
	ok = (buf.len == 0);

out:
	otlp_pb_buf_free(&buf);
	return ok;
}

/* ── main ─────────────────────────────────────────────────────── */

int
main(void)
{
	int failures = 0;

	failures += property_run(
		prop_varint_roundtrip, "prop_varint_roundtrip", 10000, 1);
	failures +=
		property_run(prop_varint_size, "prop_varint_size", 10000, 1);
	failures += property_run(
		prop_varint_extremes, "prop_varint_extremes", 1, 1);
	failures += property_run(prop_varint_field_zero_omitted,
		"prop_varint_field_zero_omitted",
		1,
		1);

	if (failures)
		printf("[property] %d varint property(ies) failed\n", failures);
	else
		printf("[property] all varint properties passed\n");

	return failures ? 1 : 0;
}
