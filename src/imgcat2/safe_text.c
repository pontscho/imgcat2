/**
 * @file safe_text.c
 * @brief [P38/P39] Untrusted text written to a terminal without control characters (see safe_text.h)
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "safe_text.h"

typedef void (*safe_emit_fn)(void *ctx, const char *p, size_t n);

/** Length of the valid UTF-8 sequence (2-4 bytes, no overlong form, no surrogate) starting at p, else 0 */
static size_t utf8_seq_len(const unsigned char *p)
{
	const unsigned char c = p[0];
	unsigned char lo = 0x80, hi = 0xBF; /* range of the second byte */
	size_t n;

	if (c >= 0xC2 && c <= 0xDF) {
		n = 2;
	} else if (c >= 0xE0 && c <= 0xEF) {
		n = 3;
		lo = c == 0xE0 ? 0xA0 : 0x80;
		hi = c == 0xED ? 0x9F : 0xBF;
	} else if (c >= 0xF0 && c <= 0xF4) {
		n = 4;
		lo = c == 0xF0 ? 0x90 : 0x80;
		hi = c == 0xF4 ? 0x8F : 0xBF;
	} else {
		return 0;
	}
	if (p[1] < lo || p[1] > hi) {
		return 0;
	}
	for (size_t i = 2; i < n; i++) {
		if (p[i] < 0x80 || p[i] > 0xBF) { /* also stops at the terminating NUL */
			return 0;
		}
	}
	return n;
}

static void emit_escaped(const unsigned char *p, size_t n, safe_emit_fn emit, void *ctx)
{
	static const char hex[] = "0123456789abcdef";
	for (size_t i = 0; i < n; i++) {
		const char e[4] = { '\\', 'x', hex[p[i] >> 4], hex[p[i] & 0xF] };
		emit(ctx, e, sizeof(e));
	}
}

/** Walk s: runs of plain text and whole UTF-8 sequences go out unchanged, control characters escaped */
static void safe_walk(const char *s, safe_emit_fn emit, void *ctx)
{
	const unsigned char *p = (const unsigned char *)s;
	while (*p != '\0') {
		size_t run = 0;
		while ((p[run] >= 0x20 && p[run] < 0x7F) || p[run] == '\n' || p[run] == '\t') { /* \n, \t: no terminal state */
			run++;
		}
		if (run > 0) {
			emit(ctx, (const char *)p, run);
			p += run;
			continue;
		}
		const size_t seq = *p >= 0x80 ? utf8_seq_len(p) : 0;
		if (seq == 2 && p[0] == 0xC2 && p[1] <= 0x9F) {
			emit_escaped(p, 2, emit, ctx); /* C1 control in UTF-8 */
			p += 2;
		} else if (seq > 0) {
			emit(ctx, (const char *)p, seq);
			p += seq;
		} else if (*p <= 0x9F) {
			emit_escaped(p, 1, emit, ctx); /* C0, DEL, raw C1 byte (not plain text, see the run above) */
			p++;
		} else {
			emit(ctx, (const char *)p, 1); /* 0xA0-0xFF outside a sequence (Latin-1 etc.) */
			p++;
		}
	}
}

static void emit_file(void *ctx, const char *p, size_t n)
{
	fwrite(p, 1, n, (FILE *)ctx);
}

void cli_fputs_safe(const char *s, FILE *f)
{
	if (s != NULL) {
		safe_walk(s, emit_file, f);
	}
}

typedef struct {
	char *buf;
	size_t size;
	size_t len;
	bool full; /* a piece did not fit: nothing more is appended */
} safe_buf_t;

static void emit_buf(void *ctx, const char *p, size_t n)
{
	safe_buf_t *b = ctx;
	if (b->full || n >= b->size - b->len) {
		b->full = true;
		return;
	}
	memcpy(b->buf + b->len, p, n);
	b->len += n;
	b->buf[b->len] = '\0';
}

const char *cli_safe_str(const char *s, char *buf, size_t size)
{
	safe_buf_t b = { buf, size, 0, false };
	buf[0] = '\0';
	if (s != NULL) {
		safe_walk(s, emit_buf, &b);
	}
	return buf;
}
