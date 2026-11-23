/**
 * @file log.c
 * @brief Library diagnostics sink (default: stderr, one line per message)
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

static _Atomic int s_level = IMG2_LOG_ERROR;
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static img2_log_fn s_fn;
static void *s_user;

void img2_set_log_callback(img2_log_fn fn, void *user)
{
	pthread_mutex_lock(&s_lock);
	s_fn = fn;
	s_user = user;
	pthread_mutex_unlock(&s_lock);
}

void img2_set_log_level(img2_log_level_t max_level)
{
	atomic_store(&s_level, (int)max_level);
}

img2_log_level_t img2_get_log_level(void)
{
	return (img2_log_level_t)atomic_load(&s_level);
}

/** @brief Length of the valid UTF-8 sequence (2-4 bytes, no overlong form, no surrogate) at p, else 0 */
static size_t log_utf8_len(const unsigned char *p)
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

/**
 * @brief [F22] Escape the message s into out (NULL: length only); returns the output length without the NUL
 *
 * Byte policy of the CLI's safe_text.c: printable ASCII, '\t', valid UTF-8 and 0xA0-0xFF outside a sequence
 * stay; C0 controls, DEL, a UTF-8 encoded C1 control (C2 80..C2 9F) and raw 0x80-0x9F bytes become "\xNN" per
 * byte (lossy: '?'). A message is one line, so '\n' is escaped too, except a single final '\n'. out may alias
 * s when the output never overtakes the unread input (lossy in place, or s moved to the end of out's buffer).
 */
static size_t log_escape(const char *s, char *out, bool lossy)
{
	static const char hex[] = "0123456789abcdef";
	const unsigned char *p = (const unsigned char *)s;
	size_t len = 0;

	while (*p != '\0') {
		const unsigned char c = *p;
		size_t n = c >= 0x80 ? log_utf8_len(p) : 1;
		bool esc;
		if (c < 0x80) {
			esc = (c < 0x20 && c != '\t' && !(c == '\n' && p[1] == '\0')) || c == 0x7F;
		} else if (n > 0) {
			esc = n == 2 && c == 0xC2 && p[1] <= 0x9F;
		} else {
			n = 1;
			esc = c <= 0x9F;
		}
		for (size_t i = 0; i < n; i++, p++) {
			const unsigned char b = *p; /* read before out (maybe aliasing p) is written */
			if (out != NULL) {
				if (!esc) {
					out[len] = (char)b;
				} else if (lossy) {
					out[len] = '?';
				} else {
					out[len] = '\\';
					out[len + 1] = 'x';
					out[len + 2] = hex[b >> 4];
					out[len + 3] = hex[b & 0xF];
				}
			}
			len += esc && !lossy ? 4 : 1;
		}
	}
	if (out != NULL) {
		out[len] = '\0';
	}
	return len;
}

void img2_log(img2_log_level_t level, const char *fmt, ...)
{
	if ((int)level > atomic_load(&s_level)) {
		return;
	}

	char buf[512];
	char *heap = NULL;
	char *msg = buf;

	va_list ap;
	va_list ap2;
	va_start(ap, fmt);
	va_copy(ap2, ap);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0) {
		buf[0] = '\0';
	} else if ((size_t)n >= sizeof(buf)) {
		heap = malloc((size_t)n + 1);
		if (heap != NULL) {
			vsnprintf(heap, (size_t)n + 1, fmt, ap2);
			msg = heap;
		}
		/* on malloc failure the truncated buf is delivered */
	}
	va_end(ap2);

	/* [F22] messages may carry file-derived text: control bytes are escaped losslessly as "\xNN" (log_escape).
	 * Nothing to escape: msg goes out unchanged. The escaped form (up to 4x) is built in buf when it fits there
	 * (msg moved to buf's end first; the output never overtakes the input), else in a heap copy. */
	size_t raw_len = strlen(msg);
	size_t esc_len = log_escape(msg, NULL, false);
	char *esc_heap = NULL;
	if (esc_len != raw_len) {
		if (msg == buf && esc_len < sizeof(buf)) {
			char *src = memmove(buf + sizeof(buf) - raw_len - 1, buf, raw_len + 1);
			log_escape(src, buf, false);
		} else if ((esc_heap = malloc(esc_len + 1)) != NULL) {
			log_escape(msg, esc_heap, false);
			msg = esc_heap;
		} else {
			/* no memory for the escaped copy: lossy fallback, every byte that would be escaped -> '?' in place */
			log_escape(msg, msg, true);
		}
	}

	pthread_mutex_lock(&s_lock);
	img2_log_fn fn = s_fn;
	void *user = s_user;
	pthread_mutex_unlock(&s_lock);

	if (fn != NULL) {
		fn(level, msg, user);
	} else {
		fputs(msg, stderr);
		fputc('\n', stderr);
	}

	free(esc_heap);
	free(heap);
}
