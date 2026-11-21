/**
 * @file log.c
 * @brief Library diagnostics sink (default: stderr, one line per message)
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
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

	/* [F22] messages may carry file-derived text: C0 controls (except '\t' and a final '\n') and DEL -> '?' */
	size_t msg_len = strlen(msg);
	for (size_t i = 0; i < msg_len; i++) {
		unsigned char c = (unsigned char)msg[i];
		if ((c < 0x20 && c != '\t' && !(c == '\n' && i + 1 == msg_len)) || c == 0x7F) {
			msg[i] = '?';
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

	free(heap);
}
