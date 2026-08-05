/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property tests for the typed field helpers and the buffer.
 *
 *   prop_field_varint_roundtrip   — encode, decode tag + value.
 *   prop_field_fixed64_roundtrip  — same for fixed64.
 *   prop_field_fixed32_roundtrip  — same for fixed32.
 *   prop_field_string_roundtrip   — same for length-delimited.
 *   prop_field_skip_defaults      — all field_* skip default values.
 *   prop_buf_growth_monotonic     — cap grows monotonically; reset
 *                                   preserves cap; ASAN-clean.
 *   prop_buf_message_overhead     — field_message emits tag+varint(len)+data.
 */
#include "prng.h"
#include "property_harness.h"
#include "decoder.h"

#include "../src/protobuf_encode.h"

#include <stdint.h>
#include <string.h>

/* ── Properties ───────────────────────────────────────────────── */

static int
prop_field_varint_roundtrip(uint64_t seed)
{
	struct prng p;
	struct otlp_pb_buf buf;
	uint64_t value;
	uint32_t field_num;
	uint64_t key, decoded;
	size_t pos = 0;
	int ok = 0;

	prng_seed(&p, seed);
	/* Ensure non-zero value (zero would be omitted). */
	value = prng_next(&p) | 1u;
	field_num = (uint32_t) (prng_next(&p) % 536870911ULL) + 1; /* 1..2^29 */

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	if (otlp_pb_field_varint(&buf, field_num, value) != OTLP_OK)
		goto out;
	if (buf.len == 0)
		goto out;

	if (decode_varint(buf.data, buf.len, &pos, &key) != OTLP_OK)
		goto out;
	if ((uint32_t) (key >> 3) != field_num)
		goto out;
	if ((int) (key & 0x7) != OTLP_PB_WIRE_VARINT)
		goto out;

	if (decode_varint(buf.data, buf.len, &pos, &decoded) != OTLP_OK)
		goto out;
	ok = (decoded == value);

out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_field_fixed64_roundtrip(uint64_t seed)
{
	struct prng p;
	struct otlp_pb_buf buf;
	uint64_t value;
	uint32_t field_num;
	uint64_t key;
	uint64_t decoded;
	size_t pos = 0;
	int ok = 0;

	prng_seed(&p, seed);
	value = prng_next(&p);
	/* fixed64 0 is omitted; force the low bit so we get a non-zero. */
	if (value == 0)
		value = 1;
	field_num = (uint32_t) (prng_next(&p) % 536870911ULL) + 1;

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	if (otlp_pb_field_fixed64(&buf, field_num, value) != OTLP_OK)
		goto out;
	if (buf.len == 0)
		goto out;

	if (decode_varint(buf.data, buf.len, &pos, &key) != OTLP_OK)
		goto out;
	if ((uint32_t) (key >> 3) != field_num)
		goto out;
	if ((int) (key & 0x7) != OTLP_PB_WIRE_FIXED64)
		goto out;

	/* 8 bytes little-endian follow the tag. */
	if (buf.len - pos < 8)
		goto out;
	decoded = 0;
	for (size_t i = 0; i < 8; i++)
		decoded |= (uint64_t) buf.data[pos + i] << (i * 8);
	ok = (decoded == value);

out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_field_fixed32_roundtrip(uint64_t seed)
{
	struct prng p;
	struct otlp_pb_buf buf;
	uint32_t value;
	uint32_t field_num;
	uint64_t key;
	uint32_t decoded;
	size_t pos = 0;
	int ok = 0;

	prng_seed(&p, seed);
	value = (uint32_t) prng_next(&p);
	if (value == 0)
		value = 1;
	field_num = (uint32_t) (prng_next(&p) % 536870911ULL) + 1;

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	if (otlp_pb_field_fixed32(&buf, field_num, value) != OTLP_OK)
		goto out;
	if (buf.len == 0)
		goto out;

	if (decode_varint(buf.data, buf.len, &pos, &key) != OTLP_OK)
		goto out;
	if ((uint32_t) (key >> 3) != field_num)
		goto out;
	if ((int) (key & 0x7) != OTLP_PB_WIRE_FIXED32)
		goto out;

	if (buf.len - pos < 4)
		goto out;
	decoded = 0;
	for (size_t i = 0; i < 4; i++)
		decoded |= (uint32_t) buf.data[pos + i] << (i * 8);
	ok = (decoded == value);

out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_field_string_roundtrip(uint64_t seed)
{
	struct prng p;
	struct otlp_pb_buf buf;
	char str[64];
	size_t slen;
	uint32_t field_num;
	uint64_t key, len_decoded;
	size_t pos = 0;
	int ok = 0;

	prng_seed(&p, seed);
	slen = (size_t) prng_u32(&p, (uint32_t) (sizeof(str) - 1)) +
		1; /* 1..63 */
	for (size_t i = 0; i < slen; i++)
		/* Printable ASCII 33..126 (avoid '\0' which truncates). */
		str[i] = (char) (prng_u32(&p, 94) + 33);
	str[slen] = '\0';
	field_num = (uint32_t) (prng_next(&p) % 536870911ULL) + 1;

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	if (otlp_pb_field_string(&buf, field_num, str) != OTLP_OK)
		goto out;
	if (buf.len == 0)
		goto out;

	if (decode_varint(buf.data, buf.len, &pos, &key) != OTLP_OK)
		goto out;
	if ((uint32_t) (key >> 3) != field_num)
		goto out;
	if ((int) (key & 0x7) != OTLP_PB_WIRE_LEN)
		goto out;
	if (decode_varint(buf.data, buf.len, &pos, &len_decoded) != OTLP_OK)
		goto out;
	if (len_decoded != slen)
		goto out;
	if (buf.len - pos != slen)
		goto out;
	ok = (memcmp(buf.data + pos, str, slen) == 0);

out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_field_skip_defaults(uint64_t seed)
{
	struct otlp_pb_buf buf;
	int ok = 0;

	(void) seed;

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;

	/* All defaults — none should emit. */
	if (otlp_pb_field_varint(&buf, 1, 0) != OTLP_OK)
		goto out;
	if (otlp_pb_field_fixed64(&buf, 2, 0) != OTLP_OK)
		goto out;
	if (otlp_pb_field_fixed32(&buf, 3, 0) != OTLP_OK)
		goto out;
	if (otlp_pb_field_string(&buf, 4, "") != OTLP_OK)
		goto out;
	if (otlp_pb_field_bytes(&buf, 5, (const uint8_t *) "", 0) != OTLP_OK)
		goto out;
	if (otlp_pb_field_message(&buf, 6, (const uint8_t *) "", 0) != OTLP_OK)
		goto out;

	ok = (buf.len == 0);

out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_buf_growth_monotonic(uint64_t seed)
{
	struct prng p;
	struct otlp_pb_buf buf;
	size_t last_cap = 0;
	size_t saved_cap;
	int ok = 0;

	prng_seed(&p, seed);
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;

	/* Emit many small varints; cap must never shrink. */
	for (int i = 0; i < 1000; i++)
	{
		if (otlp_pb_varint(&buf, prng_next(&p) % 256) != OTLP_OK)
			goto out;
		if (buf.cap < last_cap)
			goto out;
		last_cap = buf.cap;
	}

	/* Reset preserves cap, zeros len. */
	saved_cap = buf.cap;
	otlp_pb_buf_reset(&buf);
	if (buf.cap != saved_cap || buf.len != 0)
		goto out;

	ok = 1;

out:
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_field_message_overhead(uint64_t seed)
{
	struct prng p;
	struct otlp_pb_buf buf;
	struct otlp_pb_buf sub;
	uint32_t field_num;
	uint64_t key, len_decoded;
	size_t pos = 0;
	int ok = 0;

	prng_seed(&p, seed);
	field_num = (uint32_t) (prng_next(&p) % 536870911ULL) + 1;

	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	if (otlp_pb_buf_init(&sub, 0) != OTLP_OK)
		goto out_buf;
	/* Sub-message: one varint field, non-zero. */
	if (otlp_pb_field_varint(&sub, 1, 42) != OTLP_OK)
		goto out;

	if (otlp_pb_field_message(&buf, field_num, sub.data, sub.len) !=
		OTLP_OK)
		goto out;
	if (buf.len == 0)
		goto out;

	/* Decode tag + length prefix; remaining bytes should equal sub.len. */
	if (decode_varint(buf.data, buf.len, &pos, &key) != OTLP_OK)
		goto out;
	if ((uint32_t) (key >> 3) != field_num)
		goto out;
	if ((int) (key & 0x7) != OTLP_PB_WIRE_LEN)
		goto out;
	if (decode_varint(buf.data, buf.len, &pos, &len_decoded) != OTLP_OK)
		goto out;
	if (len_decoded != sub.len)
		goto out;
	if (buf.len - pos != sub.len)
		goto out;
	if (memcmp(buf.data + pos, sub.data, sub.len) != 0)
		goto out;

	ok = 1;

out:
	otlp_pb_buf_free(&sub);
out_buf:
	otlp_pb_buf_free(&buf);
	return ok;
}

/* ── main ─────────────────────────────────────────────────────── */

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_field_varint_roundtrip,
		"prop_field_varint_roundtrip",
		5000,
		1);
	failures += property_run(prop_field_fixed64_roundtrip,
		"prop_field_fixed64_roundtrip",
		5000,
		1);
	failures += property_run(prop_field_fixed32_roundtrip,
		"prop_field_fixed32_roundtrip",
		5000,
		1);
	failures += property_run(prop_field_string_roundtrip,
		"prop_field_string_roundtrip",
		5000,
		1);
	failures += property_run(
		prop_field_skip_defaults, "prop_field_skip_defaults", 1, 1);
	failures += property_run(
		prop_buf_growth_monotonic, "prop_buf_growth_monotonic", 100, 1);
	failures += property_run(prop_field_message_overhead,
		"prop_field_message_overhead",
		1000,
		1);

	if (failures)
		printf("[property] %d encoder property(ies) failed\n",
			failures);
	else
		printf("[property] all encoder properties passed\n");

	return failures ? 1 : 0;
}
