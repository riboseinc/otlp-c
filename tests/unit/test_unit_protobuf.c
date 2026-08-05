// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for the protobuf wire encoder. Complements the property
// tests with specific known-answer test vectors from the Protobuf
// spec.

#include "protobuf_encode.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>

static int test_varint_zero(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	assert(s == OTLP_OK);

	s = otlp_pb_varint(&buf, 0);
	assert(s == OTLP_OK);
	assert(buf.len == 1);
	assert(buf.data[0] == 0x00);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int test_varint_one(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	assert(s == OTLP_OK);

	s = otlp_pb_varint(&buf, 1);
	assert(s == OTLP_OK);
	assert(buf.len == 1);
	assert(buf.data[0] == 0x01);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int test_varint_127(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	assert(s == OTLP_OK);

	s = otlp_pb_varint(&buf, 127);
	assert(s == OTLP_OK);
	assert(buf.len == 1);
	assert(buf.data[0] == 0x7f);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int test_varint_128(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	assert(s == OTLP_OK);

	s = otlp_pb_varint(&buf, 128);
	assert(s == OTLP_OK);
	assert(buf.len == 2);
	assert(buf.data[0] == 0x80);
	assert(buf.data[1] == 0x01);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int test_varint_300(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	assert(s == OTLP_OK);

	s = otlp_pb_varint(&buf, 300);
	assert(s == OTLP_OK);
	assert(buf.len == 2);
	assert(buf.data[0] == 0xac);
	assert(buf.data[1] == 0x02);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int test_fixed64_endian(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	assert(s == OTLP_OK);

	s = otlp_pb_fixed64(&buf, 0x0102030405060708ULL);
	assert(s == OTLP_OK);
	assert(buf.len == 8);
	/* Protobuf fixed64 is little-endian. */
	assert(buf.data[0] == 0x08);
	assert(buf.data[1] == 0x07);
	assert(buf.data[2] == 0x06);
	assert(buf.data[3] == 0x05);
	assert(buf.data[4] == 0x04);
	assert(buf.data[5] == 0x03);
	assert(buf.data[6] == 0x02);
	assert(buf.data[7] == 0x01);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int test_fixed32_endian(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	assert(s == OTLP_OK);

	s = otlp_pb_fixed32(&buf, 0x01020304);
	assert(s == OTLP_OK);
	assert(buf.len == 4);
	/* Protobuf fixed32 is little-endian. */
	assert(buf.data[0] == 0x04);
	assert(buf.data[1] == 0x03);
	assert(buf.data[2] == 0x02);
	assert(buf.data[3] == 0x01);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int test_string_encoding(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 32);
	assert(s == OTLP_OK);

	s = otlp_pb_string(&buf, "hello");
	assert(s == OTLP_OK);
	/* Length prefix (1 byte = 0x05) + 5 bytes of content. */
	assert(buf.len == 6);
	assert(buf.data[0] == 0x05);
	assert(memcmp(buf.data + 1, "hello", 5) == 0);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int test_tag_encoding(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	assert(s == OTLP_OK);

	/* Field 1, wire type 2 (length-delimited): key = (1 << 3) | 2 = 0x0a */
	s = otlp_pb_tag(&buf, 1, OTLP_PB_WIRE_LEN);
	assert(s == OTLP_OK);
	assert(buf.len == 1);
	assert(buf.data[0] == 0x0a);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int test_buf_growth(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;
	size_t i;

	s = otlp_pb_buf_init(&buf, 4);
	assert(s == OTLP_OK);
	assert(buf.cap == 4);

	/* Write more than initial capacity. */
	for (i = 0; i < 100; i++) {
		s = otlp_pb_varint(&buf, i);
		assert(s == OTLP_OK);
	}

	/* Buffer should have grown. */
	assert(buf.cap > 4);
	assert(buf.len > 0);

	otlp_pb_buf_free(&buf);
	return 0;
}

static int test_buf_reset(void)
{
	struct otlp_pb_buf buf = { 0 };
	otlp_status_t s;

	s = otlp_pb_buf_init(&buf, 16);
	assert(s == OTLP_OK);

	s = otlp_pb_varint(&buf, 42);
	assert(s == OTLP_OK);
	assert(buf.len > 0);

	otlp_pb_buf_reset(&buf);
	assert(buf.len == 0);
	assert(buf.cap > 0);
	assert(buf.data != NULL);

	otlp_pb_buf_free(&buf);
	return 0;
}

int main(void)
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

	if (failures)
		printf("[unit-protobuf] FAIL (%d test(s))\n", failures);
	else
		printf("[unit-protobuf] PASS (11 tests)\n");

	return failures ? 1 : 0;
}
