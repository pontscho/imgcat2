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
