/**
 * @file test_jpeg_log.c
 * @brief libjpeg warnings routed through img2_log (core/log_jpeg.c), never written to stderr by libjpeg
 *
 * A small JPEG from our encoder gets junk bytes inserted before a marker; libjpeg skips them with the warning
 * "Corrupt JPEG data: 3 extraneous bytes before marker 0xNN". The test captures fd 2 through a pipe: with a log
 * callback installed nothing may reach it, the callback gets the text at IMG2_LOG_WARN; with the default sink
 * the text is the one libjpeg's own output_message printed ("<message>\n"), filtered by the log level.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/core/log.h"
#include "../../libimg2/decoders/decoder.h"
#include "../../libimg2/encoders/encoder.h"
#include "../ctest.h"

#define MAX_MSGS 8

typedef struct {
	int calls;
	img2_log_level_t level[MAX_MSGS];
	char msg[MAX_MSGS][256];
} jlog_capture_t;

static void jlog_cb(img2_log_level_t level, const char *msg, void *user)
{
	jlog_capture_t *cap = (jlog_capture_t *)user;
	if (cap->calls < MAX_MSGS) {
		cap->level[cap->calls] = level;
		snprintf(cap->msg[cap->calls], sizeof(cap->msg[0]), "%s", msg);
	}
	cap->calls++;
}

/** fd 2 redirected into a pipe (never a file under /tmp) */
typedef struct {
	int saved_fd;
	int read_fd;
} stderr_capture_t;

static void stderr_begin(stderr_capture_t *c)
{
	int fds[2];
	ASSERT_EQUAL(0, pipe(fds));
	fflush(stderr);
	c->saved_fd = dup(STDERR_FILENO);
	ASSERT_TRUE(c->saved_fd >= 0);
	ASSERT_TRUE(dup2(fds[1], STDERR_FILENO) >= 0);
	close(fds[1]);
	c->read_fd = fds[0];
}

/** Restore fd 2; returns everything written to it meanwhile (NUL-terminated, at most cap - 1 bytes) */
static size_t stderr_end(stderr_capture_t *c, char *buf, size_t cap)
{
	fflush(stderr);
	dup2(c->saved_fd, STDERR_FILENO); /* closes the last write end: read() below ends at EOF */
	close(c->saved_fd);
	size_t n = 0;
	for (;;) {
		ssize_t r = read(c->read_fd, buf + n, cap - 1 - n);
		if (r <= 0) {
			break;
		}
		n += (size_t)r;
		if (n == cap - 1) {
			break;
		}
	}
	buf[n] = '\0';
	close(c->read_fd);
	return n;
}

/** Copy of jpg with 3 junk bytes (no 0xFF) inserted before each marker listed in markers (0-terminated) */
static uint8_t *insert_junk(const uint8_t *jpg, size_t len, const uint8_t *markers, size_t *out_len, int *inserted)
{
	static const uint8_t junk[3] = { 0x01, 0x02, 0x03 };
	uint8_t *out = malloc(len + 3 * 8);
	if (out == NULL) {
		return NULL;
	}
	size_t o = 0;
	size_t p = 2; /* after SOI */
	memcpy(out, jpg, 2);
	o = 2;
	*inserted = 0;
	/* walk the header segments up to and including SOS (FF xx, 16-bit big-endian length) */
	while (p + 4 <= len && jpg[p] == 0xFF) {
		uint8_t m = jpg[p + 1];
		size_t seg = 2 + (((size_t)jpg[p + 2] << 8) | jpg[p + 3]);
		for (const uint8_t *k = markers; *k != 0; k++) {
			if (*k == m && *inserted < 8) {
				memcpy(out + o, junk, sizeof(junk));
				o += sizeof(junk);
				(*inserted)++;
			}
		}
		if (p + seg > len) {
			break;
		}
		memcpy(out + o, jpg + p, seg);
		o += seg;
		p += seg;
		if (m == 0xDA) {
			break;
		}
	}
	memcpy(out + o, jpg + p, len - p); /* entropy-coded data and EOI */
	o += len - p;
	*out_len = o;
	return out;
}

/** 32x32 gradient JPEG from img2_encode_jpeg() with junk before the given markers; free() the result */
static uint8_t *make_corrupt_jpeg(const uint8_t *markers, size_t *len, int *inserted)
{
	img2_image_t *img = img2_image_alloc(32, 32);
	if (img == NULL) {
		return NULL;
	}
	for (uint32_t y = 0; y < 32; y++) {
		for (uint32_t x = 0; x < 32; x++) {
			img2_image_set_pixel(img, x, y, (uint8_t)(x * 8), (uint8_t)(y * 8), 128, 255);
		}
	}
	uint8_t *jpg = NULL;
	size_t jpg_len = 0;
	img2_status_t st = img2_encode_jpeg(img, 90, &jpg, &jpg_len);
	img2_image_destroy(img);
	if (st != IMG2_OK) {
		return NULL;
	}
	uint8_t *bad = insert_junk(jpg, jpg_len, markers, len, inserted);
	free(jpg);
	return bad;
}

static img2_status_t decode(const uint8_t *data, size_t len, int deblock)
{
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	opts.jpeg_deblock = deblock;
	img2_frames_t fr = { 0 };
	img2_status_t st = img2_decode_jpeg(data, len, &opts, &fr);
	if (st == IMG2_OK && (fr.count != 1 || fr.frames[0]->width != 32 || fr.frames[0]->height != 32)) {
		st = IMG2_E_INTERNAL;
	}
	img2_frames_free(&fr);
	return st;
}

static const uint8_t k_sof[] = { 0xC0, 0 };
static const uint8_t k_sof_sos[] = { 0xC0, 0xDA, 0 };

CTEST(jpeg_log, warning_reaches_callback_not_stderr)
{
	size_t len = 0;
	int inserted = 0;
	uint8_t *bad = make_corrupt_jpeg(k_sof, &len, &inserted);
	ASSERT_NOT_NULL(bad);
	ASSERT_EQUAL(1, inserted);

	img2_log_level_t saved = img2_get_log_level();
	img2_set_log_level(IMG2_LOG_WARN);

	/* deblock 0: plain libjpeg; 1: jpeg-quantsmooth when built (same error manager) */
	for (int deblock = 0; deblock <= 1; deblock++) {
		jlog_capture_t cap = { 0 };
		img2_set_log_callback(jlog_cb, &cap);
		stderr_capture_t sc;
		stderr_begin(&sc);
		img2_status_t st = decode(bad, len, deblock);
		char err[512];
		size_t n = stderr_end(&sc, err, sizeof(err));
		img2_set_log_callback(NULL, NULL);

		if (n != 0) {
			fprintf(stderr, "deblock %d: unexpected stderr output: %s\n", deblock, err);
		}
		ASSERT_EQUAL(0, (int)n);
		ASSERT_EQUAL(IMG2_OK, st);
		ASSERT_EQUAL(1, cap.calls);
		ASSERT_EQUAL(IMG2_LOG_WARN, cap.level[0]);
		ASSERT_STR("Corrupt JPEG data: 3 extraneous bytes before marker 0xc0", cap.msg[0]);
	}

	img2_set_log_level(saved);
	free(bad);
}

CTEST(jpeg_log, only_first_warning_shown)
{
	/* libjpeg's emit_message semantics kept: one warning per image at the default trace_level */
	size_t len = 0;
	int inserted = 0;
	uint8_t *bad = make_corrupt_jpeg(k_sof_sos, &len, &inserted);
	ASSERT_NOT_NULL(bad);
	ASSERT_EQUAL(2, inserted);

	img2_log_level_t saved = img2_get_log_level();
	img2_set_log_level(IMG2_LOG_DEBUG);
	jlog_capture_t cap = { 0 };
	img2_set_log_callback(jlog_cb, &cap);
	stderr_capture_t sc;
	stderr_begin(&sc);
	img2_status_t st = decode(bad, len, 0);
	char err[512];
	size_t n = stderr_end(&sc, err, sizeof(err));
	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(saved);

	ASSERT_EQUAL(0, (int)n);
	ASSERT_EQUAL(IMG2_OK, st);
	ASSERT_EQUAL(1, cap.calls);
	ASSERT_EQUAL(IMG2_LOG_WARN, cap.level[0]);
	ASSERT_STR("Corrupt JPEG data: 3 extraneous bytes before marker 0xc0", cap.msg[0]);
	free(bad);
}

CTEST(jpeg_log, default_sink_same_text_and_level_filtered)
{
	size_t len = 0;
	int inserted = 0;
	uint8_t *bad = make_corrupt_jpeg(k_sof, &len, &inserted);
	ASSERT_NOT_NULL(bad);

	img2_log_level_t saved = img2_get_log_level();
	img2_set_log_callback(NULL, NULL);

	/* WARN (the CLI's level): the default sink prints what libjpeg printed itself */
	img2_set_log_level(IMG2_LOG_WARN);
	stderr_capture_t sc;
	stderr_begin(&sc);
	img2_status_t st = decode(bad, len, 0);
	char err[512];
	stderr_end(&sc, err, sizeof(err));
	ASSERT_EQUAL(IMG2_OK, st);
	ASSERT_STR("Corrupt JPEG data: 3 extraneous bytes before marker 0xc0\n", err);

	/* ERROR (the library default): a warning is not shown at all */
	img2_set_log_level(IMG2_LOG_ERROR);
	stderr_begin(&sc);
	st = decode(bad, len, 0);
	size_t n = stderr_end(&sc, err, sizeof(err));
	ASSERT_EQUAL(IMG2_OK, st);
	ASSERT_EQUAL(0, (int)n);

	img2_set_log_level(saved);
	free(bad);
}
