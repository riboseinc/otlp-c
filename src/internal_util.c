/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Internal utilities shared across src/ files. See internal_util.h.
 */
#include "internal_util.h"

#include <stdlib.h>
#include <string.h>

char *
otlp_dup_str(const char *s)
{
	size_t	len;
	char   *out;

	if (!s)
		return NULL;
	len = strlen(s);
	out = malloc(len + 1);
	if (!out)
		return NULL;
	memcpy(out, s, len + 1);
	return out;
}

uint8_t *
otlp_dup_bytes(const uint8_t *src, size_t len)
{
	uint8_t *out;

	if (len == 0)
		return NULL;
	if (!src)
		return NULL;
	out = malloc(len);
	if (!out)
		return NULL;
	memcpy(out, src, len);
	return out;
}
