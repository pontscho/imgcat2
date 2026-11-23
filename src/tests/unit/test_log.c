/**
 * @file test_log.c
 * @brief Unit tests for the img2 log module (callback, level filter, default sink)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../libimg2/core/log.h"
#include "../ctest.h"

typedef struct {
	int calls;
	img2_log_level_t level;
	char msg[4096];
} log_capture_t;

static void capture_cb(img2_log_level_t level, const char *msg, void *user)
{
	log_capture_t *cap = (log_capture_t *)user;
	cap->calls++;
	cap->level = level;
	snprintf(cap->msg, sizeof(cap->msg), "%s", msg);
}

CTEST(log, callback_receives_level_and_message)
{
	img2_log_level_t saved = img2_get_log_level();
	log_capture_t cap = { 0 };

	img2_set_log_level(IMG2_LOG_WARN);
	img2_set_log_callback(capture_cb, &cap);
	img2_log(IMG2_LOG_WARN, "Warning: value %d of %s", 42, "x");

	ASSERT_EQUAL(1, cap.calls);
	ASSERT_EQUAL(IMG2_LOG_WARN, cap.level);
	ASSERT_STR("Warning: value 42 of x", cap.msg);
	ASSERT_NULL(strchr(cap.msg, '\n'));

	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(saved);
}

CTEST(log, level_filtering)
{
	img2_log_level_t saved = img2_get_log_level();
	log_capture_t cap = { 0 };

	img2_set_log_level(IMG2_LOG_ERROR);
	ASSERT_EQUAL(IMG2_LOG_ERROR, img2_get_log_level());
	img2_set_log_callback(capture_cb, &cap);

	img2_log(IMG2_LOG_DEBUG, "dropped %d", 1);
	img2_log(IMG2_LOG_INFO, "dropped %d", 2);
	img2_log(IMG2_LOG_WARN, "dropped %d", 3);
	ASSERT_EQUAL(0, cap.calls);

	img2_log(IMG2_LOG_ERROR, "kept %d", 4);
	ASSERT_EQUAL(1, cap.calls);
	ASSERT_EQUAL(IMG2_LOG_ERROR, cap.level);
	ASSERT_STR("kept 4", cap.msg);

	img2_set_log_level(IMG2_LOG_DEBUG);
	img2_log(IMG2_LOG_DEBUG, "debug %d", 5);
	ASSERT_EQUAL(2, cap.calls);
	ASSERT_STR("debug 5", cap.msg);

	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(saved);
}

CTEST(log, null_callback_restores_default_sink)
{
	img2_log_level_t saved = img2_get_log_level();
	log_capture_t cap = { 0 };

	img2_set_log_level(IMG2_LOG_ERROR);
	img2_set_log_callback(capture_cb, &cap);
	img2_set_log_callback(NULL, NULL);

	/* Capture stderr through a pipe (never a file under /tmp) */
	int fds[2];
	ASSERT_EQUAL(0, pipe(fds));
	fflush(stderr);
	int saved_fd = dup(STDERR_FILENO);
	ASSERT_TRUE(saved_fd >= 0);
	ASSERT_TRUE(dup2(fds[1], STDERR_FILENO) >= 0);

	img2_log(IMG2_LOG_ERROR, "Error: default sink %d", 7);
	fflush(stderr);

	dup2(saved_fd, STDERR_FILENO);
	close(saved_fd);
	close(fds[1]);

	char buf[128] = { 0 };
	ssize_t n = read(fds[0], buf, sizeof(buf) - 1);
	close(fds[0]);

	ASSERT_EQUAL(0, cap.calls);
	ASSERT_TRUE(n > 0);
	ASSERT_STR("Error: default sink 7\n", buf);

	img2_set_log_level(saved);
}

CTEST(log, long_message_delivered_intact)
{
	img2_log_level_t saved = img2_get_log_level();
	log_capture_t cap = { 0 };

	char *longmsg = malloc(2001);
	ASSERT_NOT_NULL(longmsg);
	for (int i = 0; i < 2000; i++) {
		longmsg[i] = (char)('a' + (i % 26));
	}
	longmsg[2000] = '\0';

	img2_set_log_level(IMG2_LOG_ERROR);
	img2_set_log_callback(capture_cb, &cap);
	img2_log(IMG2_LOG_ERROR, "%s", longmsg);

	ASSERT_EQUAL(1, cap.calls);
	ASSERT_EQUAL(2000, (int)strlen(cap.msg));
	ASSERT_STR(longmsg, cap.msg);

	free(longmsg);
	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(saved);
}

/* ===== [F22] lossless escaping of control bytes ===== */

typedef struct {
	const char *in;
	const char *out;
} esc_case_t;

CTEST(log, escapes_control_bytes_losslessly)
{
	static const esc_case_t cases[] = {
		{ "plain text", "plain text" },
		{ "\x1b[31mred", "\\x1b[31mred" },           /* ESC sequence */
		{ "a\nb", "a\\x0ab" },                        /* '\n' inside the line */
		{ "end\n", "end\n" },                         /* a single final '\n' is kept */
		{ "two\n\n", "two\\x0a\n" },
		{ "t\tab", "t\tab" },                         /* '\t' raw */
		{ "\x01" "x\x7f", "\\x01x\\x7f" },            /* C0, DEL */
		{ "caf\xc3\xa9", "caf\xc3\xa9" },             /* valid UTF-8 raw: 2, 3 and 4 bytes */
		{ "\xe2\x82\xac \xe0\xa0\x80 \xed\x9f\xbf", "\xe2\x82\xac \xe0\xa0\x80 \xed\x9f\xbf" },
		{ "\xf0\x9f\x98\x80 \xf4\x8f\xbf\xbf", "\xf0\x9f\x98\x80 \xf4\x8f\xbf\xbf" },
		{ "\xc2\xa0" "nbsp", "\xc2\xa0" "nbsp" },     /* U+00A0 is not a C1 control */
		{ "\xc2\x9b" "2J", "\\xc2\\x9b2J" },          /* UTF-8 encoded C1 control (CSI) */
		{ "\x80x", "\\x80x" },                        /* stray continuation byte */
		{ "\xe9t\xe9", "\xe9t\xe9" },                 /* Latin-1 byte outside a sequence raw */
		{ "\xc1\x81", "\xc1\\x81" },                  /* overlong lead byte: not a sequence */
		{ "\xe0\x80\x80", "\xe0\\x80\\x80" },         /* overlong 3-byte form */
		{ "\xed\xa0\x80", "\xed\xa0\\x80" },          /* surrogate */
		{ "\xf0\x9f\x98", "\xf0\\x9f\\x98" },         /* sequence cut short by the NUL */
		{ "\xe2\x82x", "\xe2\\x82x" },                /* bad continuation byte */
	};
	img2_log_level_t saved = img2_get_log_level();
	log_capture_t cap = { 0 };
	img2_set_log_level(IMG2_LOG_ERROR);
	img2_set_log_callback(capture_cb, &cap);
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		img2_log(IMG2_LOG_ERROR, "%s", cases[i].in);
		ASSERT_STR(cases[i].out, cap.msg);
	}
	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(saved);
}

/** n ESC bytes followed by m 'a' bytes, as raw text and as the expected escaped text */
static void esc_build(size_t n, size_t m, char *raw, char *want)
{
	for (size_t i = 0; i < n; i++) {
		raw[i] = '\x1b';
		memcpy(want + 4 * i, "\\x1b", 4);
	}
	memset(raw + n, 'a', m);
	memset(want + 4 * n, 'a', m);
	raw[n + m] = '\0';
	want[4 * n + m] = '\0';
}

CTEST(log, escaped_message_sizes)
{
	/* { ESC count, 'a' count }: escaped form in the 512-byte stack buffer (exactly 511 bytes, the ESC bytes at
	 * the start so the in-place rewrite is checked), one byte too long for it, raw text fits but the escaped
	 * text does not, and a raw text already on the heap */
	static const size_t sizes[][2] = { { 3, 40 }, { 100, 111 }, { 100, 112 }, { 200, 0 }, { 50, 600 } };
	static char raw[1024], want[4096];
	img2_log_level_t saved = img2_get_log_level();
	log_capture_t cap = { 0 };
	img2_set_log_level(IMG2_LOG_ERROR);
	img2_set_log_callback(capture_cb, &cap);
	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		esc_build(sizes[i][0], sizes[i][1], raw, want);
		img2_log(IMG2_LOG_ERROR, "%s", raw);
		ASSERT_STR(want, cap.msg);
	}
	/* long text with escapes in the middle and a kept final '\n' */
	memset(raw, 'b', 700);
	memcpy(raw + 300, "\x1b\n\xc2\x9b", 4);
	memcpy(raw + 699, "\n", 2);
	img2_log(IMG2_LOG_ERROR, "%s", raw);
	ASSERT_EQUAL(700 - 4 + 16, (int)strlen(cap.msg));
	ASSERT_TRUE(strncmp(cap.msg + 300, "\\x1b\\x0a\\xc2\\x9bb", 17) == 0);
	ASSERT_STR("b\n", cap.msg + strlen(cap.msg) - 2);
	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(saved);
}
