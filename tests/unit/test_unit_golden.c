// SPDX-License-Identifier: BSD-3-Clause
//
// Golden vectors (v0.5.101): our encoders vs the REFERENCE
// opentelemetry-proto serialization. tests/golden/generate.py
// built the embedded payloads with the reference Python classes;
// this test reconstructs the same fixtures through otlp-c's
// public API + internal encoders and compares both sides as
// canonical protobuf field trees (parsed with the library's
// bounds-checked reader).
//
// Comparison is SEMANTIC, not byte-for-byte: fields are matched
// by (field number, wire type) with repeated order preserved, so
// valid field reordering never fails — but any drift in field
// numbers, wire types, zigzag/packing, presence rules, or values
// does, with a path to the mismatch.
//
// This is the payload-level counterpart of the schema pins in
// test_unit_wire_numbers.c: pins catch schema-table drift; these
// goldens catch encoding-semantics drift against the reference
// implementation. Keep fixtures in lockstep with generate.py.

#include "../golden/golden_vectors.h"
#include "../test_util.h"
#include "otlp_messages.h"
#include "protobuf_decode.h"
#include "protobuf_encode.h"

#include <otlp-c/log.h>
#include <otlp-c/metric.h>
#include <otlp-c/span.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define T_BASE 1700000000000000000ULL

/* ── Canonical field tree ─────────────────────────────────────── */

#define GOLD_MAX_NODES 512
#define GOLD_MAX_DEPTH 8

struct gold_node
{
	uint32_t field;
	int wt;
	uint64_t v; /* varint / fixed value (host order) */
	const uint8_t *bytes; /* LEN payload */
	size_t bytes_len;
	struct gold_node *kids; /* non-NULL: payload parsed as a
				 * (possibly empty) submessage */
	size_t n_kids;
	bool consumed;
};

/* One pool PER SIDE: a shared pool would let the second parse
 * overwrite the first tree — the comparator would then compare
 * ours-with-itself and pass vacuously (caught by mutation test:
 * a fixture flip sailed through until the pools were split). */
static struct gold_node g_pool_a[GOLD_MAX_NODES];
static struct gold_node g_pool_b[GOLD_MAX_NODES];
static struct gold_node *g_pool; /* current allocation side */
static size_t g_pool_used;

static struct gold_node *
pool_take(size_t n)
{
	struct gold_node *p;

	if (g_pool_used + n > GOLD_MAX_NODES)
		return NULL;
	p = &g_pool[g_pool_used];
	g_pool_used += n;
	memset(p, 0, n * sizeof(*p));
	return p;
}

/* Parse a buffer into a node array. Returns NULL if the buffer is
 * not a well-formed message (caller keeps it as opaque bytes) or
 * the pool/depth budget is exhausted. */
static struct gold_node *
gold_parse(const uint8_t *buf, size_t len, int depth, size_t *n_out)
{
	struct otlp_pb_reader r;
	size_t n = 0;
	struct gold_node *nodes;

	if (depth > GOLD_MAX_DEPTH)
		return NULL;
	/* Count first so the pool allocation is exact. */
	{
		struct otlp_pb_reader c;

		otlp_pb_reader_init(&c, buf, len);
		while (c.pos < c.len)
		{
			uint32_t field;
			int wt;

			if (!otlp_pb_read_key(&c, &field, &wt))
				return NULL;
			if (!otlp_pb_skip(&c, wt))
				return NULL;
			n++;
		}
	}
	nodes = pool_take(n);
	if (!nodes)
		return NULL;
	otlp_pb_reader_init(&r, buf, len);
	for (size_t i = 0; i < n; i++)
	{
		uint32_t field;
		int wt;
		struct gold_node *nd = &nodes[i];

		if (!otlp_pb_read_key(&r, &field, &wt))
			return NULL;
		nd->field = field;
		nd->wt = wt;
		if (wt == OTLP_PB_WIRE_VARINT)
		{
			if (!otlp_pb_read_varint(&r, &nd->v))
				return NULL;
		}
		else if (wt == OTLP_PB_WIRE_FIXED32)
		{
			uint32_t v32;

			if (!otlp_pb_read_fixed32(&r, &v32))
				return NULL;
			nd->v = v32;
		}
		else if (wt == OTLP_PB_WIRE_FIXED64)
		{
			if (!otlp_pb_read_fixed64(&r, &nd->v))
				return NULL;
		}
		else if (wt == OTLP_PB_WIRE_LEN)
		{
			if (!otlp_pb_read_len(&r, &nd->bytes, &nd->bytes_len))
				return NULL;
			nd->kids = gold_parse(nd->bytes,
				nd->bytes_len,
				depth + 1,
				&nd->n_kids);
		}
		else
			return NULL;
	}
	*n_out = n;
	return nodes;
}

/* Compare two node arrays semantically. Repeated fields match in
 * order; singular fields match regardless of emission position. */
static bool
gold_equal(struct gold_node *a,
	size_t a_n,
	struct gold_node *b,
	size_t b_n,
	const char *path)
{
	if (a_n != b_n)
	{
		fprintf(stderr,
			"[golden] MISMATCH %s: %zu fields vs %zu\n",
			path,
			a_n,
			b_n);
		return false;
	}
	for (size_t i = 0; i < a_n; i++)
	{
		struct gold_node *an = &a[i];
		struct gold_node *bn = NULL;
		char sub[128];

		for (size_t j = 0; j < b_n; j++)
		{
			if (!b[j].consumed && b[j].field == an->field &&
				b[j].wt == an->wt)
			{
				bn = &b[j];
				break;
			}
		}
		if (!bn)
		{
			fprintf(stderr,
				"[golden] MISMATCH %s: field %u (wt %d) "
				"missing from one side\n",
				path,
				an->field,
				an->wt);
			return false;
		}
		bn->consumed = true;

		snprintf(sub,
			sizeof(sub),
			"%s.f%u[%" PRIuPTR "]",
			path,
			an->field,
			i);
		if (an->wt == OTLP_PB_WIRE_LEN)
		{
			if (an->kids && bn->kids)
			{
				if (!gold_equal(an->kids,
					    an->n_kids,
					    bn->kids,
					    bn->n_kids,
					    sub))
					return false;
			}
			else if (an->bytes_len != bn->bytes_len ||
				memcmp(an->bytes, bn->bytes, an->bytes_len) !=
					0)
			{
				fprintf(stderr,
					"[golden] MISMATCH %s: payload differs "
					"(len %zu vs %zu)\n",
					sub,
					an->bytes_len,
					bn->bytes_len);
				return false;
			}
		}
		else if (an->v != bn->v)
		{
			fprintf(stderr,
				"[golden] MISMATCH %s: %" PRIu64 " vs %" PRIu64
				"\n",
				sub,
				an->v,
				bn->v);
			return false;
		}
	}
	return true;
}

static bool
compare_vector(const char *name,
	const uint8_t *golden,
	size_t golden_len,
	const struct otlp_pb_buf *ours)
{
	struct gold_node *ga, *gb;
	size_t ga_n = 0, gb_n = 0;
	bool ok;

	g_pool = g_pool_a;
	g_pool_used = 0;
	ga = gold_parse(golden, golden_len, 0, &ga_n);
	check_true(ga != NULL);
	g_pool = g_pool_b;
	g_pool_used = 0;
	gb = gold_parse(ours->data, ours->len, 0, &gb_n);
	check_true(gb != NULL);

	ok = gold_equal(ga, ga_n, gb, gb_n, name);
	printf("[golden] %-7s %zu bytes vs %zu bytes — %s\n",
		name,
		golden_len,
		ours->len,
		ok ? "match" : "MISMATCH");
	return ok;
}

/* ── Fixtures (mirror tests/golden/generate.py exactly) ───────── */

static void
fill_ids(uint8_t *buf, uint8_t from, size_t n)
{
	for (size_t i = 0; i < n; i++)
		buf[i] = (uint8_t)(from + i);
}

static bool
build_traces(struct otlp_pb_buf *out)
{
	uint8_t tid[16], sid[8], parent[8], ltid[16], lsid[8];
	const otlp_span_t *spans[1];
	otlp_span_t *s = otlp_span_create("golden-span");

	check_true(s != NULL);
	fill_ids(tid, 0x00, 16);
	fill_ids(sid, 0x10, 8);
	fill_ids(parent, 0x20, 8);
	fill_ids(ltid, 0x30, 16);
	fill_ids(lsid, 0x40, 8);

	check_ok(otlp_span_set_trace_id(s, tid));
	check_ok(otlp_span_set_span_id(s, sid));
	check_ok(otlp_span_set_parent_span_id(s, parent));
	check_ok(otlp_span_set_trace_state(s, "vendor1=1,vendor2=2"));
	check_ok(otlp_span_set_kind(s, OTLP_SPAN_KIND_SERVER));
	check_ok(otlp_span_set_start_time(s, T_BASE + 123456789));
	check_ok(otlp_span_set_end_time(s, T_BASE + 987654321));

	check_ok(otlp_span_set_attribute_string(s, "http.method", "GET"));
	check_ok(otlp_span_set_attribute_int(s, "http.status_code", 404));
	check_ok(
		otlp_span_set_attribute_int(s, "neg", INT64_C(-1234567890123)));
	check_ok(otlp_span_set_attribute_double(s, "ratio", 0.25));
	check_ok(otlp_span_set_attribute_bool(s, "ok", true));
	{
		static const uint8_t raw[4] = { 0x00, 0x01, 0xfe, 0xff };

		check_ok(otlp_span_set_attribute_bytes(s, "raw", raw, 4));
	}
	{
		static const otlp_value_t list_items[3] = {
			{ .type = OTLP_VALUE_STRING,
				.v = { .string_val = "a" } },
			{ .type = OTLP_VALUE_INT64, .v = { .int64_val = 7 } },
			{ .type = OTLP_VALUE_BOOL, .v = { .bool_val = false } },
		};
		static const otlp_kv_t map_entries[2] = {
			{ .key = "inner",
				.value = { .type = OTLP_VALUE_STRING,
					.v = { .string_val = "v" } } },
			{ .key = "n",
				.value = { .type = OTLP_VALUE_DOUBLE,
					.v = { .double_val = 1.5 } } },
		};

		check_ok(otlp_span_set_attribute_array(
			s, "list", list_items, 3));
		check_ok(otlp_span_set_attribute_kvlist(
			s, "map", map_entries, 2));
	}

	check_ok(otlp_span_set_status(s, OTLP_STATUS_CODE_ERROR, "boom"));
	check_ok(otlp_span_add_event(s, "evt", T_BASE + 500000000));
	check_ok(otlp_span_set_event_attribute_string(s, "k", "v"));
	check_ok(otlp_span_add_link(s, ltid, lsid));
	check_ok(otlp_span_set_link_attribute_bool(s, "linked", true));

	spans[0] = s;
	check_ok(otlp_encode_export_trace_service_request(
		out, "golden-svc", NULL, 0, "golden-scope", "1.2.3", spans, 1));
	otlp_span_free(s);
	return true;
}

static otlp_metric_t *
make_gauge(void)
{
	otlp_metric_t *m = otlp_metric_create(
		OTLP_METRIC_GAUGE, "golden-gauge", "ms", "gd", NULL, 0);

	check_true(m != NULL);
	check_ok(otlp_metric_record(m, 42.5));
	check_ok(otlp_metric_set_time(m, T_BASE + 555000000));
	check_ok(otlp_metric_set_attribute_string(m, "axis", "x"));
	return m;
}

static otlp_metric_t *
make_hist(void)
{
	static const double bounds[3] = { 10.5, 20.5, 30.5 };
	static const double values[7] = { 1.25, 12.5, 15, 22, 25, 30, 64 };
	otlp_metric_t *m = otlp_metric_create(
		OTLP_METRIC_HISTOGRAM, "golden-hist", "B", NULL, bounds, 3);

	check_true(m != NULL);
	for (size_t i = 0; i < 7; i++)
		check_ok(otlp_metric_record(m, values[i]));
	check_ok(otlp_metric_set_start_time(m, T_BASE));
	check_ok(otlp_metric_set_time(m, T_BASE + 111111111));
	check_ok(otlp_metric_set_aggregation_temporality(
		m, OTLP_AGG_TEMP_CUMULATIVE));
	return m;
}

static otlp_metric_t *
make_exp_hist(void)
{
	static const uint64_t pos[3] = { 1, 0, 2 };
	static const uint64_t neg[1] = { 5 };
	static const double values[8] = { 8, 16, 32, 0.5, 4, 2, 1, 36 };
	otlp_metric_t *m = otlp_metric_create(
		OTLP_METRIC_EXP_HISTOGRAM, "golden-eh", NULL, NULL, NULL, 0);

	check_true(m != NULL);
	for (size_t i = 0; i < 8; i++)
		check_ok(otlp_metric_record(m, values[i]));
	check_ok(otlp_metric_record(m, 0.0));
	check_ok(otlp_metric_record(m, 0.0));
	check_ok(otlp_metric_set_exp_histogram(m, -3, 4, pos, 3, -2, neg, 1));
	check_ok(otlp_metric_set_time(m, T_BASE + 222222222));
	check_ok(otlp_metric_set_aggregation_temporality(
		m, OTLP_AGG_TEMP_CUMULATIVE));
	return m;
}

static otlp_metric_t *
make_counter(void)
{
	otlp_metric_t *m = otlp_metric_create(
		OTLP_METRIC_COUNTER, "golden-counter", "1", NULL, NULL, 0);

	check_true(m != NULL);
	check_ok(otlp_metric_record(m, 3.0));
	check_ok(otlp_metric_record(m, 4.0));
	check_ok(otlp_metric_set_time(m, T_BASE + 333333333));
	check_ok(otlp_metric_set_aggregation_temporality(
		m, OTLP_AGG_TEMP_CUMULATIVE));
	return m;
}

static bool
build_metrics(struct otlp_pb_buf *out)
{
	const otlp_metric_t *metrics[4];

	metrics[0] = make_gauge();
	metrics[1] = make_hist();
	metrics[2] = make_exp_hist();
	metrics[3] = make_counter();
	check_ok(otlp_encode_export_metrics_service_request(out,
		"golden-svc",
		NULL,
		0,
		"golden-scope",
		"1.2.3",
		metrics,
		4));
	for (size_t i = 0; i < 4; i++)
		otlp_metric_free((otlp_metric_t *) metrics[i]);
	return true;
}

static bool
build_logs(struct otlp_pb_buf *out)
{
	uint8_t tid[16], sid[8];
	const otlp_log_record_t *logs[2];
	otlp_log_record_t *lr1 =
		otlp_log_record_create(OTLP_SEVERITY_INFO, "golden message");
	otlp_log_record_t *lr2 =
		otlp_log_record_create(OTLP_SEVERITY_WARN, "second");

	fill_ids(tid, 0x50, 16);
	fill_ids(sid, 0x60, 8);

	check_true(lr1 != NULL && lr2 != NULL);
	check_ok(otlp_log_record_set_timestamp(lr1, T_BASE + 222222222));
	check_ok(otlp_log_record_set_severity_text(lr1, "INFO"));
	check_ok(otlp_log_record_set_attribute_string(lr1, "k", "v"));
	check_ok(otlp_log_record_set_attribute_int(lr1, "code", 404));
	check_ok(otlp_log_record_set_trace_id(lr1, tid));
	check_ok(otlp_log_record_set_span_id(lr1, sid));

	check_ok(otlp_log_record_set_timestamp(lr2, T_BASE + 333333333));
	check_ok(otlp_log_record_set_severity_text(lr2, "WARN"));

	logs[0] = lr1;
	logs[1] = lr2;
	check_ok(otlp_encode_export_logs_service_request(
		out, "golden-svc", NULL, 0, "golden-scope", "1.2.3", logs, 2));
	otlp_log_record_free(lr1);
	otlp_log_record_free(lr2);
	return true;
}

int
main(void)
{
	struct otlp_pb_buf ours = { 0 };
	int failures = 0;

	check_ok(otlp_pb_buf_init(&ours, 1024));
	check_true(build_traces(&ours));
	failures += !compare_vector(
		"traces", GOLDEN_TRACES, GOLDEN_TRACES_LEN, &ours);
	otlp_pb_buf_free(&ours);

	check_ok(otlp_pb_buf_init(&ours, 1024));
	check_true(build_metrics(&ours));
	failures += !compare_vector(
		"metrics", GOLDEN_METRICS, GOLDEN_METRICS_LEN, &ours);
	otlp_pb_buf_free(&ours);

	check_ok(otlp_pb_buf_init(&ours, 1024));
	check_true(build_logs(&ours));
	failures +=
		!compare_vector("logs", GOLDEN_LOGS, GOLDEN_LOGS_LEN, &ours);
	otlp_pb_buf_free(&ours);

	if (failures)
		printf("[golden] FAIL (%d vector(s))\n", failures);
	else
		printf("[golden] PASS (3 vectors)\n");
	return failures ? 1 : 0;
}
