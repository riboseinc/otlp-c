// SPDX-License-Identifier: BSD-3-Clause
//
// Unit tests for the protobuf wire encoder. Complements the property
// tests with specific known-answer test vectors from the Protobuf
// spec.

#include "exporter_otel.h"
#include "../test_util.h"
#include "protobuf_decode.h"
#include "protobuf_encode.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

static int
test_varint_zero(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	check_true(s == OTLP_OK);

	s = otlp_pb_varint(&buf, 0);
	check_true(s == OTLP_OK);
	check_true(buf.len == 1);
	check_true(buf.data[0] == 0x00);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int
test_varint_one(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	check_true(s == OTLP_OK);

	s = otlp_pb_varint(&buf, 1);
	check_true(s == OTLP_OK);
	check_true(buf.len == 1);
	check_true(buf.data[0] == 0x01);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int
test_varint_127(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	check_true(s == OTLP_OK);

	s = otlp_pb_varint(&buf, 127);
	check_true(s == OTLP_OK);
	check_true(buf.len == 1);
	check_true(buf.data[0] == 0x7f);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int
test_varint_128(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	check_true(s == OTLP_OK);

	s = otlp_pb_varint(&buf, 128);
	check_true(s == OTLP_OK);
	check_true(buf.len == 2);
	check_true(buf.data[0] == 0x80);
	check_true(buf.data[1] == 0x01);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int
test_varint_300(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	check_true(s == OTLP_OK);

	s = otlp_pb_varint(&buf, 300);
	check_true(s == OTLP_OK);
	check_true(buf.len == 2);
	check_true(buf.data[0] == 0xac);
	check_true(buf.data[1] == 0x02);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int
test_fixed64_endian(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	check_true(s == OTLP_OK);

	s = otlp_pb_fixed64(&buf, 0x0102030405060708ULL);
	check_true(s == OTLP_OK);
	check_true(buf.len == 8);
	/* Protobuf fixed64 is little-endian. */
	check_true(buf.data[0] == 0x08);
	check_true(buf.data[1] == 0x07);
	check_true(buf.data[2] == 0x06);
	check_true(buf.data[3] == 0x05);
	check_true(buf.data[4] == 0x04);
	check_true(buf.data[5] == 0x03);
	check_true(buf.data[6] == 0x02);
	check_true(buf.data[7] == 0x01);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int
test_fixed32_endian(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	check_true(s == OTLP_OK);

	s = otlp_pb_fixed32(&buf, 0x01020304);
	check_true(s == OTLP_OK);
	check_true(buf.len == 4);
	/* Protobuf fixed32 is little-endian. */
	check_true(buf.data[0] == 0x04);
	check_true(buf.data[1] == 0x03);
	check_true(buf.data[2] == 0x02);
	check_true(buf.data[3] == 0x01);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int
test_string_encoding(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 32);
	check_true(s == OTLP_OK);

	s = otlp_pb_string(&buf, "hello");
	check_true(s == OTLP_OK);
	/* Length prefix (1 byte = 0x05) + 5 bytes of content. */
	check_true(buf.len == 6);
	check_true(buf.data[0] == 0x05);
	check_true(memcmp(buf.data + 1, "hello", 5) == 0);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int
test_tag_encoding(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	check_true(s == OTLP_OK);

	/* Field 1, wire type 2 (length-delimited): key = (1 << 3) | 2 = 0x0a */
	s = otlp_pb_tag(&buf, 1, OTLP_PB_WIRE_LEN);
	check_true(s == OTLP_OK);
	check_true(buf.len == 1);
	check_true(buf.data[0] == 0x0a);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int
test_buf_growth(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;
	size_t i;

	/* With SBO, buf_init(4) uses the 64-byte inline buffer (cap=64). */
	s = otlp_pb_buf_init(&buf, 4);
	check_true(s == OTLP_OK);
	check_true(buf.cap >= 4);
	check_true(buf.data != NULL);

	/* Write more than SBO capacity (192 bytes since v0.5.86;
	 * was 64 — each varint ≤ 2 bytes for i < 16384). */
	for (i = 0; i < 300; i++)
	{
		s = otlp_pb_varint(&buf, i);
		check_true(s == OTLP_OK);
	}

	/* Buffer should have grown beyond SBO onto heap. */
	check_true(buf.cap > OTLP_PB_SBO_SIZE);
	check_true(buf.owns_heap);
	check_true(buf.len > 0);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int
test_buf_reset(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	check_true(s == OTLP_OK);

	s = otlp_pb_varint(&buf, 42);
	check_true(s == OTLP_OK);
	check_true(buf.len > 0);

	otlp_pb_buf_reset(&buf);
	check_true(buf.len == 0);
	check_true(buf.cap > 0);
	check_true(buf.data != NULL);

	otlp_pb_buf_free(&buf);
	return 0;
}

/* ── Wire-format decoder (v0.5.96) ────────────────────────────── */

static int
test_decode_key_roundtrip(void)
{
	struct otlp_pb_buf buf = { 0 };
	struct otlp_pb_reader r;
	otlp_status_t s;
	uint32_t field = 0;
	int wt = -1;

	s = otlp_pb_buf_init(&buf, 16);
	check_true(s == OTLP_OK);
	s = otlp_pb_tag(&buf, 5, OTLP_PB_WIRE_LEN);
	check_true(s == OTLP_OK);

	otlp_pb_reader_init(&r, buf.data, buf.len);
	check_true(otlp_pb_read_key(&r, &field, &wt));
	check_true(field == 5);
	check_true(wt == OTLP_PB_WIRE_LEN);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int
test_decode_varint_roundtrip(void)
{
	static const uint64_t vectors[] = {
		0, 1, 300, 16383, 16384, UINT64_MAX
	};
	struct otlp_pb_buf buf = { 0 };
	size_t i;
	int ok = 1;

	check_true(otlp_pb_buf_init(&buf, 16) == OTLP_OK);
	for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++)
		check_true(otlp_pb_varint(&buf, vectors[i]) == OTLP_OK);
	{
		struct otlp_pb_reader r;

		otlp_pb_reader_init(&r, buf.data, buf.len);
		for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++)
		{
			uint64_t v = 0;

			if (!otlp_pb_read_varint(&r, &v) || v != vectors[i])
				ok = 0;
		}
		/* Reader fully consumed. */
		if (otlp_pb_read_varint(&r, &(uint64_t){ 0 }))
			ok = 0;
	}
	otlp_pb_buf_free(&buf);
	check_true(ok);
	return 0;
}

static int
test_decode_len_roundtrip(void)
{
	struct otlp_pb_buf buf = { 0 };
	struct otlp_pb_reader r;
	const uint8_t *data = NULL;
	size_t len = 0;

	check_true(otlp_pb_buf_init(&buf, 16) == OTLP_OK);
	check_true(otlp_pb_string(&buf, "hello") == OTLP_OK);

	otlp_pb_reader_init(&r, buf.data, buf.len);
	check_true(otlp_pb_read_len(&r, &data, &len));
	check_true(len == 5);
	check_true(memcmp(data, "hello", 5) == 0);
	/* Empty-buffer edge. */
	otlp_pb_reader_init(&r, buf.data, 0);
	check_true(!otlp_pb_read_len(&r, &data, &len));

	otlp_pb_buf_free(&buf);
	return 0;
}

static int
test_decode_malformed(void)
{
	static const uint8_t trunc_varint[] = { 0x80 };
	static const uint8_t trunc_len[] = {
		0x0a, 0x10, 'x'
	}; /* len 16 > rest */
	static const uint8_t field_zero[] = { 0x00,
		0x01 }; /* field 0 invalid */
	static const uint8_t group[] = { 0x0b }; /* wire type 3 (group) */
	struct otlp_pb_reader r;
	const uint8_t *data = NULL;
	size_t len = 0;
	uint32_t field = 0;
	int wt = -1;

	otlp_pb_reader_init(&r, trunc_varint, sizeof(trunc_varint));
	check_true(!otlp_pb_read_key(&r, &field, &wt));

	otlp_pb_reader_init(&r, trunc_len, sizeof(trunc_len));
	check_true(otlp_pb_read_key(&r, &field, &wt));
	check_true(field == 1 && wt == OTLP_PB_WIRE_LEN);
	check_true(!otlp_pb_read_len(&r, &data, &len));

	otlp_pb_reader_init(&r, field_zero, sizeof(field_zero));
	check_true(!otlp_pb_read_key(&r, &field, &wt));

	otlp_pb_reader_init(&r, group, sizeof(group));
	check_true(otlp_pb_read_key(&r, &field, &wt));
	check_true(!otlp_pb_skip(&r, wt));
	return 0;
}

static int
test_decode_skip_all_wire_types(void)
{
	struct otlp_pb_buf buf = { 0 };
	struct otlp_pb_reader r;
	uint32_t field = 0;
	int wt = -1;

	check_true(otlp_pb_buf_init(&buf, 32) == OTLP_OK);
	/* field 1 varint, field 2 fixed64, field 3 len, field 4 fixed32 */
	check_true(otlp_pb_tag(&buf, 1, OTLP_PB_WIRE_VARINT) == OTLP_OK);
	check_true(otlp_pb_varint(&buf, 150) == OTLP_OK);
	check_true(otlp_pb_tag(&buf, 2, OTLP_PB_WIRE_FIXED64) == OTLP_OK);
	check_true(otlp_pb_fixed64(&buf, 0x0102030405060708ULL) == OTLP_OK);
	check_true(otlp_pb_tag(&buf, 3, OTLP_PB_WIRE_LEN) == OTLP_OK);
	check_true(otlp_pb_string(&buf, "ab") == OTLP_OK);
	check_true(otlp_pb_tag(&buf, 4, OTLP_PB_WIRE_FIXED32) == OTLP_OK);
	check_true(otlp_pb_fixed32(&buf, 0x0a0b0c0dU) == OTLP_OK);

	otlp_pb_reader_init(&r, buf.data, buf.len);
	for (;;)
	{
		if (!otlp_pb_read_key(&r, &field, &wt))
			break;
		check_true(otlp_pb_skip(&r, wt));
	}
	check_true(r.pos == buf.len);

	otlp_pb_buf_free(&buf);
	return 0;
}

/* Build an Export*ServiceResponse body: field 5 wrapping a
 * PartialSuccess {rejected=1 varint, error_message=2 string}. */
static void
build_response(struct otlp_pb_buf *resp,
	bool with_rejected,
	uint64_t rejected,
	const char *message)
{
	struct otlp_pb_buf sub = { 0 };

	check_true(otlp_pb_buf_init(&sub, 16) == OTLP_OK);
	if (with_rejected)
	{
		check_true(
			otlp_pb_tag(&sub, 1, OTLP_PB_WIRE_VARINT) == OTLP_OK);
		check_true(otlp_pb_varint(&sub, rejected) == OTLP_OK);
	}
	if (message)
	{
		check_true(otlp_pb_tag(&sub, 2, OTLP_PB_WIRE_LEN) == OTLP_OK);
		check_true(otlp_pb_string(&sub, message) == OTLP_OK);
	}
	/* Field 1 — the REAL response format (v1.0.2: this fixture
	 * built field 5, matching the wrong table). */
	check_true(
		otlp_pb_field_message(resp, 1, sub.data, sub.len) == OTLP_OK);
	otlp_pb_buf_free(&sub);
}

static int
test_decode_partial_success(void)
{
	struct otlp_pb_buf resp = { 0 };
	int64_t rejected = 0;
	const char *msg = NULL;
	size_t msg_len = 0;

	check_true(otlp_pb_buf_init(&resp, 16) == OTLP_OK);
	build_response(&resp, true, 3, "queue full");
	check_true(otlp_exporter_otel_decode_partial_success(
		resp.data, resp.len, &rejected, &msg, &msg_len));
	check_true(rejected == 3);
	check_true(msg && msg_len == 10 && strncmp(msg, "queue full", 10) == 0);
	otlp_pb_buf_free(&resp);

	/* Empty body: no partial_success — false. */
	check_true(!otlp_exporter_otel_decode_partial_success(
		NULL, 0, &rejected, &msg, &msg_len));

	/* Explicitly-present-but-empty submessage (0a 00): a conformant
	 * proto3 serializer never emits this (presence rule — and note
	 * otlp_pb_field_message omits it too), but the decoder must
	 * accept it: true, zero outputs. */
	{
		static const uint8_t empty_ps[] = { 0x0a, 0x00 };

		check_true(otlp_exporter_otel_decode_partial_success(
			empty_ps, sizeof(empty_ps), &rejected, &msg, &msg_len));
		check_true(rejected == 0 && msg == NULL && msg_len == 0);
	}

	/* Unknown fields around it are skipped. */
	check_true(otlp_pb_buf_init(&resp, 16) == OTLP_OK);
	check_true(otlp_pb_tag(&resp, 1, OTLP_PB_WIRE_VARINT) == OTLP_OK);
	check_true(otlp_pb_varint(&resp, 42) == OTLP_OK);
	build_response(&resp, true, 7, NULL);
	check_true(otlp_exporter_otel_decode_partial_success(
		resp.data, resp.len, &rejected, &msg, &msg_len));
	check_true(rejected == 7 && msg == NULL);
	otlp_pb_buf_free(&resp);

	/* Duplicate partial_success: last wins (proto3 merge). */
	check_true(otlp_pb_buf_init(&resp, 16) == OTLP_OK);
	build_response(&resp, true, 1, "first");
	build_response(&resp, true, 9, "second");
	check_true(otlp_exporter_otel_decode_partial_success(
		resp.data, resp.len, &rejected, &msg, &msg_len));
	check_true(rejected == 9 && msg && strncmp(msg, "second", 6) == 0);
	otlp_pb_buf_free(&resp);

	/* Truncated submessage length: malformed — false. */
	{
		static const uint8_t bad[] = { 0x0a, 0x10, 0x08, 0x01 };

		check_true(!otlp_exporter_otel_decode_partial_success(
			bad, sizeof(bad), &rejected, &msg, &msg_len));
	}
	return 0;
}

int
main(void)
{
	int failures = 0;

	failures += test_varint_zero();
	failures += test_varint_one();
	failures += test_varint_127();
	failures += test_varint_128();
	failures += test_varint_300();
	failures += test_fixed64_endian();
	failures += test_fixed32_endian();
	failures += test_string_encoding();
	failures += test_tag_encoding();
	failures += test_buf_growth();
	failures += test_buf_reset();
	failures += test_decode_key_roundtrip();
	failures += test_decode_varint_roundtrip();
	failures += test_decode_len_roundtrip();
	failures += test_decode_malformed();
	failures += test_decode_skip_all_wire_types();
	failures += test_decode_partial_success();

	if (failures)
		printf("[unit-protobuf] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-protobuf] PASS (17 tests)\n");

	return failures ? 1 : 0;
}
