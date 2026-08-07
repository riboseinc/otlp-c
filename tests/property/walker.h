/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Shared test-only protobuf walker. Used by property tests that
 * verify OTLP wire shapes: walks one level at a time and lets the
 * caller descend explicitly (recursive search is ambiguous because
 * OTLP field numbers are scoped per-message).
 *
 * Duplicated was present in test_property_metrics.c,
 * test_property_logs.c, test_property_events_context.c. Extracted
 * here to keep test code DRY.
 */
#ifndef OTLP_C_TEST_WALKER_H
#define OTLP_C_TEST_WALKER_H

#include "decoder.h"
#include "../src/protobuf_encode.h"

#include <stddef.h>
#include <stdint.h>

static inline int
walker_find_at_level(const uint8_t *data, size_t pos, size_t end,
		     uint32_t fnum, int *wt_out,
		     size_t *val_pos, size_t *val_len)
{
	while (pos < end) {
		uint32_t fn = 0;
		int	    wt = 0;
		size_t	   vstart;
		size_t	   vlen = 0;
		otlp_status_t st = decode_tag(data, end, &pos, &fn, &wt);

		if (st != OTLP_OK)
			return 0;
		vstart = pos;
		if (wt == OTLP_PB_WIRE_VARINT) {
			uint64_t v;
			if (decode_varint(data, end, &pos, &v) != OTLP_OK)
				return 0;
			vlen = pos - vstart;
		} else if (wt == OTLP_PB_WIRE_FIXED64) {
			if (pos + 8 > end)
				return 0;
			pos += 8;
			vlen = 8;
		} else if (wt == OTLP_PB_WIRE_FIXED32) {
			if (pos + 4 > end)
				return 0;
			pos += 4;
			vlen = 4;
		} else if (wt == OTLP_PB_WIRE_LEN) {
			uint64_t l;
			if (decode_varint(data, end, &pos, &l) != OTLP_OK)
				return 0;
			vlen = (size_t) l;
			vstart = pos;
			if (pos + vlen > end)
				return 0;
			pos += vlen;
		} else {
			return 0;
		}
		if (fn == fnum) {
			if (wt_out && val_pos && val_len) {
				*wt_out  = wt;
				*val_pos = vstart;
				*val_len = vlen;
			}
			return 1;
		}
	}
	return 0;
}

static inline int
walker_descend(const uint8_t *data, size_t *pos, size_t *end,
	       uint32_t fnum)
{
	int    wt = 0;
	size_t vp = 0;
	size_t vl = 0;

	if (!walker_find_at_level(data, *pos, *end, fnum, &wt, &vp, &vl))
		return 0;
	if (wt != OTLP_PB_WIRE_LEN)
		return 0;
	*pos = vp;
	*end = vp + vl;
	return 1;
}

#endif
