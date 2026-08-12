/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Property tests for the OTLP logs encoder.
 *
 * Walks the wire one level at a time and asserts the OTLP envelope:
 *   ExportLogsServiceRequest { ResourceLogs{1} }
 *     → ResourceLogs { Resource{1}, ScopeLogs{2} }
 *       → ScopeLogs { Scope{1}, LogRecord{2} }
 *         → LogRecord { time{1}, severity_number{2}, severity_text{3},
 *                       body{5}, attributes{6}, trace_id{9}, span_id{10} }
 *
 * Tests:
 *   prop_logs_empty_request         — no logs + no service → 0 bytes.
 *   prop_logs_severity_present      — INFO severity → field 2 varint.
 *   prop_logs_severity_omitted      — UNSPECIFIED → no field 2.
 *   prop_logs_body_string_roundtrip — body string round-trips via AnyValue.
 *   prop_logs_trace_correlation     — trace_id/span_id bytes preserved.
 *   prop_logs_attributes_roundtrip  — int attribute round-trips.
 */
#include "decoder.h"
#include "walker.h"
#include "prng.h"
#include "property_harness.h"

#include "../src/log_internal.h"
#include "../src/otlp_messages.h"
#include "../src/protobuf_encode.h"

#include <otlp-c/log.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Single-level walker (same shape as test_property_metrics.c). */
/* Walk to the LogRecord level. Returns 1 if descent succeeded. */
static int
descend_to_log_record(const uint8_t *data, size_t len,
		      size_t *lr_pos, size_t *lr_end)
{
	size_t pos = 0, end = len;

	if (!walker_descend(data, &pos, &end, 1))	/* ResourceLogs */
		return 0;
	if (!walker_descend(data, &pos, &end, 2))	/* ScopeLogs */
		return 0;
	if (!walker_descend(data, &pos, &end, 2))	/* LogRecord */
		return 0;
	*lr_pos = pos;
	*lr_end = end;
	return 1;
}

static int
prop_logs_empty_request(uint64_t seed)
{
	struct otlp_pb_buf buf = { 0 };
	int		       ok = 0;

	(void) seed;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		return 0;
	if (otlp_encode_export_logs_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, NULL, 0) == OTLP_OK)
		ok = (buf.len == 0);
	otlp_pb_buf_free(&buf);
	return ok;
}

static int
prop_logs_severity_present(uint64_t seed)
{
	otlp_log_record_t   *lr;
	struct otlp_pb_buf   buf = { 0 };
	const otlp_log_record_t *arr[1] = { NULL };
	int		    ok = 0;
	size_t		    pos, end;
	int		    wt;
	size_t		    vp, vl;

	(void) seed;
	lr = otlp_log_record_create(OTLP_SEVERITY_INFO, "hi");
	if (!lr)
		return 0;
	arr[0] = lr;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_logs_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;
	if (!descend_to_log_record(buf.data, buf.len, &pos, &end))
		goto out_buf;
	if (walker_find_at_level(buf.data, pos, end, 2, &wt, &vp, &vl) &&
	    wt == OTLP_PB_WIRE_VARINT) {
		size_t  p2 = vp;
		uint64_t v;

		if (decode_varint(buf.data, end, &p2, &v) == OTLP_OK &&
		    v == OTLP_SEVERITY_INFO)
			ok = 1;
	}

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_log_record_free(lr);
	return ok;
}

static int
prop_logs_severity_omitted(uint64_t seed)
{
	otlp_log_record_t   *lr;
	struct otlp_pb_buf   buf = { 0 };
	const otlp_log_record_t *arr[1] = { NULL };
	int		    ok = 0;

	(void) seed;
	lr = otlp_log_record_create(OTLP_SEVERITY_UNSPECIFIED, NULL);
	if (!lr)
		return 0;
	arr[0] = lr;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_logs_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;
	/* Empty body, no attrs, no severity → no log_records emitted →
	 * ScopeLogs empty → ResourceLogs empty → request 0 bytes. */
	ok = (buf.len == 0);

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_log_record_free(lr);
	return ok;
}

static int
prop_logs_body_string_roundtrip(uint64_t seed)
{
	struct prng	      p;
	otlp_log_record_t  *lr;
	struct otlp_pb_buf   buf = { 0 };
	const otlp_log_record_t *arr[1] = { NULL };
	char		      body[32];
	size_t		      blen;
	int		      ok = 0;
	size_t		      pos, end;
	int		      wt;
	size_t		      vp, vl;

	prng_seed(&p, seed);
	blen = (size_t) prng_u32(&p, 28) + 1;
	for (size_t i = 0; i < blen; i++)
		body[i] = (char)(prng_u32(&p, 94) + 33);
	body[blen] = '\0';

	lr = otlp_log_record_create(OTLP_SEVERITY_ERROR, body);
	if (!lr)
		return 0;
	arr[0] = lr;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_logs_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;
	if (!descend_to_log_record(buf.data, buf.len, &pos, &end))
		goto out_buf;
	/* body{5} is LEN sub-message (AnyValue). */
	if (walker_find_at_level(buf.data, pos, end, 5, &wt, &vp, &vl) &&
	    wt == OTLP_PB_WIRE_LEN) {
		/* AnyValue: string_value{1} is LEN. */
		size_t ap = vp, ae = vp + vl;

		if (walker_find_at_level(buf.data, ap, ae, 1, &wt, &vp, &vl) &&
		    wt == OTLP_PB_WIRE_LEN && vl == blen &&
		    memcmp(buf.data + vp, body, blen) == 0)
			ok = 1;
	}

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_log_record_free(lr);
	return ok;
}

static int
prop_logs_trace_correlation(uint64_t seed)
{
	struct prng	      p;
	otlp_log_record_t  *lr;
	struct otlp_pb_buf   buf = { 0 };
	const otlp_log_record_t *arr[1] = { NULL };
	uint8_t	      trace_id[16];
	uint8_t	      span_id[8];
	int		      ok = 0;
	size_t		      pos, end, i;
	int		      wt;
	size_t		      vp, vl;

	prng_seed(&p, seed);
	for (i = 0; i < 16; i++)
		trace_id[i] = (uint8_t) prng_u32(&p, 256);
	for (i = 0; i < 8; i++)
		span_id[i] = (uint8_t) prng_u32(&p, 256);

	lr = otlp_log_record_create(OTLP_SEVERITY_WARN, "with trace");
	if (!lr)
		return 0;
	otlp_log_record_set_trace_id(lr, trace_id);
	otlp_log_record_set_span_id(lr, span_id);
	arr[0] = lr;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_logs_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;
	if (!descend_to_log_record(buf.data, buf.len, &pos, &end))
		goto out_buf;

	ok = 0;
	if (walker_find_at_level(buf.data, pos, end, 9, &wt, &vp, &vl) &&
	    wt == OTLP_PB_WIRE_LEN && vl == 16 &&
	    memcmp(buf.data + vp, trace_id, 16) == 0)
		ok |= 1;
	if (walker_find_at_level(buf.data, pos, end, 10, &wt, &vp, &vl) &&
	    wt == OTLP_PB_WIRE_LEN && vl == 8 &&
	    memcmp(buf.data + vp, span_id, 8) == 0)
		ok |= 2;
	ok = (ok == 3);

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_log_record_free(lr);
	return ok;
}

static int
prop_logs_attributes_roundtrip(uint64_t seed)
{
	struct prng	      p;
	otlp_log_record_t  *lr;
	struct otlp_pb_buf   buf = { 0 };
	const otlp_log_record_t *arr[1] = { NULL };
	int64_t	      v;
	int		      ok = 0;
	size_t		      pos, end;
	int		      wt;
	size_t		      vp, vl;

	prng_seed(&p, seed);
	v = (int64_t) prng_next(&p);
	lr = otlp_log_record_create(OTLP_SEVERITY_INFO, "attrs");
	if (!lr)
		return 0;
	otlp_log_record_set_attribute_int(lr, "k", v);
	arr[0] = lr;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_logs_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;
	if (!descend_to_log_record(buf.data, buf.len, &pos, &end))
		goto out_buf;
	/* attributes{6} is LEN sub-message (KeyValue). */
	if (walker_find_at_level(buf.data, pos, end, 6, &wt, &vp, &vl) &&
	    wt == OTLP_PB_WIRE_LEN) {
		/* KeyValue: key{1}, value{2}. */
		size_t kp = vp, ke = vp + vl;

		if (walker_find_at_level(buf.data, kp, ke, 2, &wt, &vp, &vl) &&
		    wt == OTLP_PB_WIRE_LEN) {
			/* AnyValue: int_value{3} VARINT. */
			size_t ap = vp, ae = vp + vl;

			if (walker_find_at_level(buf.data, ap, ae, 3, &wt, &vp, &vl) &&
			    wt == OTLP_PB_WIRE_VARINT) {
				size_t  p2 = vp;
				uint64_t got;

				if (decode_varint(buf.data, ae, &p2, &got) == OTLP_OK &&
				    (int64_t) got == v)
					ok = 1;
			}
		}
	}

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_log_record_free(lr);
	return ok;
}

static int
prop_logs_trace_id_only_no_zero_span_id(uint64_t seed)
{
	otlp_log_record_t  *lr;
	struct otlp_pb_buf   buf = { 0 };
	const otlp_log_record_t *arr[1] = { NULL };
	uint8_t	      trace_id[16];
	int		      ok = 0;
	size_t		      pos, end, i;
	int		      wt;
	size_t		      vp, vl;

	(void) seed;
	for (i = 0; i < 16; i++)
		trace_id[i] = (uint8_t)(i + 1);  /* non-zero pattern */

	lr = otlp_log_record_create(OTLP_SEVERITY_INFO, "trace-only");
	if (!lr)
		return 0;
	otlp_log_record_set_trace_id(lr, trace_id);
	/* Deliberately do NOT set span_id — verify it's omitted, not
	 * emitted as all-zeros. Pre-v0.5.50 had a single has_trace flag
	 * that emitted both with span_id as 8 zero bytes. */
	arr[0] = lr;
	if (otlp_pb_buf_init(&buf, 0) != OTLP_OK)
		goto out;
	if (otlp_encode_export_logs_service_request(
		    &buf, NULL, NULL, 0, NULL, NULL, arr, 1) != OTLP_OK)
		goto out_buf;
	if (!descend_to_log_record(buf.data, buf.len, &pos, &end))
		goto out_buf;

	/* trace_id present. */
	ok = (walker_find_at_level(buf.data, pos, end, 9, &wt, &vp, &vl) &&
	      wt == OTLP_PB_WIRE_LEN && vl == 16 &&
	      memcmp(buf.data + vp, trace_id, 16) == 0);
	/* span_id absent. */
	ok = ok && !walker_find_at_level(buf.data, pos, end, 10, &wt, &vp, &vl);

out_buf:
	otlp_pb_buf_free(&buf);
out:
	otlp_log_record_free(lr);
	return ok;
}

int
main(void)
{
	int failures = 0;

	failures += property_run(prop_logs_empty_request,
				 "prop_logs_empty_request", 1, 1);
	failures += property_run(prop_logs_severity_present,
				 "prop_logs_severity_present", 1, 1);
	failures += property_run(prop_logs_severity_omitted,
				 "prop_logs_severity_omitted", 1, 1);
	failures += property_run(prop_logs_body_string_roundtrip,
				 "prop_logs_body_string_roundtrip", 200, 1);
	failures += property_run(prop_logs_trace_correlation,
				 "prop_logs_trace_correlation", 50, 1);
	failures += property_run(prop_logs_attributes_roundtrip,
				 "prop_logs_attributes_roundtrip", 200, 1);
	failures += property_run(prop_logs_trace_id_only_no_zero_span_id,
				 "prop_logs_trace_id_only_no_zero_span_id", 5, 1);

	if (failures)
		printf("[property] %d logs property(ies) failed\n", failures);
	else
		printf("[property] all logs properties passed\n");
	return failures ? 1 : 0;
}
