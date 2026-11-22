/**
 * @file test_animation_timing.c
 * @brief delay_ms / loop_count reported by img2_decode for animated inputs (S11)
 *
 * Expected values are the facts recorded in S0 by probe_anim.py
 * (.claude/tmp/img2-golden/anim-facts.tsv):
 *   smoke_anim.gif   3 frames  100,100,100 ms  loop 0
 *   smoke_anim.png   3 frames  100,100,100 ms  loop 0
 *   smoke_anim.webp  3 frames  100,100,100 ms  loop 0
 * Public API only (<img2/img2.h>).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../ctest.h"

#ifndef IMG2_TEST_SRC_DIR
#define IMG2_TEST_SRC_DIR "../src/tests"
#endif

/* From .claude/tmp/img2-golden/anim-facts.tsv (S0) */
#define ANIM_FRAMES 3
#define ANIM_DELAY_MS 100u
#define ANIM_LOOP 0

static uint8_t *load_file(const char *rel, size_t *len)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/%s", IMG2_TEST_SRC_DIR, rel);
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *buf = (n > 0) ? malloc((size_t)n) : NULL;
	if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);
	*len = (size_t)n;
	return buf;
}

/* Decode a buffer and check count / every delay / loop count */
static void check_timing(const uint8_t *data, size_t len, int count, uint32_t delay_ms, int loop_count)
{
	img2_frames_t fr;
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	ASSERT_EQUAL(IMG2_OK, img2_decode(data, len, &opts, &fr));
	ASSERT_EQUAL(count, fr.count);
	ASSERT_EQUAL(loop_count, fr.loop_count);
	for (int i = 0; i < fr.count; i++) {
		ASSERT_NOT_NULL(fr.frames[i]);
		ASSERT_EQUAL(delay_ms, fr.frames[i]->delay_ms);
	}
	img2_frames_free(&fr);
}

static void check_file(const char *rel, int count, uint32_t delay_ms, int loop_count)
{
	size_t len = 0;
	uint8_t *data = load_file(rel, &len);
	ASSERT_NOT_NULL(data);
	check_timing(data, len, count, delay_ms, loop_count);
	free(data);
}

CTEST(anim_timing, gif)
{
	if (!img2_can_decode(IMG2_MIME_GIF)) {
		return; /* skipped: GIF decoder not built */
	}
	check_file("data/anim/smoke_anim.gif", ANIM_FRAMES, ANIM_DELAY_MS, ANIM_LOOP);
}

CTEST(anim_timing, gif_without_netscape_plays_once)
{
	if (!img2_can_decode(IMG2_MIME_GIF)) {
		return; /* skipped: GIF decoder not built */
	}
	size_t len = 0;
	uint8_t *data = load_file("data/anim/smoke_anim.gif", &len);
	ASSERT_NOT_NULL(data);

	/* Cut the 19-byte application extension: 21 FF 0B "NETSCAPE2.0" 03 01 lo hi 00 */
	uint8_t *ext = NULL;
	for (size_t i = 3; i + 16 <= len; i++) {
		if (memcmp(data + i, "NETSCAPE2.0", 11) == 0 && data[i - 3] == 0x21 && data[i - 2] == 0xFF && data[i - 1] == 0x0B) {
			ext = data + i - 3;
			break;
		}
	}
	ASSERT_NOT_NULL(ext);
	size_t off = (size_t)(ext - data);
	ASSERT_TRUE(off + 19 <= len);
	ASSERT_EQUAL(0x00, ext[18]);
	memmove(ext, ext + 19, len - off - 19);
	len -= 19;

	check_timing(data, len, ANIM_FRAMES, ANIM_DELAY_MS, 1);
	free(data);
}

CTEST(anim_timing, apng)
{
#if IMG2_HAS_APNG
	check_file("data/anim/smoke_anim.png", ANIM_FRAMES, ANIM_DELAY_MS, ANIM_LOOP);
#else
	return; /* skipped: APNG not supported by this libpng */
#endif
}

CTEST(anim_timing, webp)
{
	if (!img2_can_decode(IMG2_MIME_WEBP)) {
		return; /* skipped: WebP decoder not built */
	}
	check_file("data/anim/smoke_anim.webp", ANIM_FRAMES, ANIM_DELAY_MS, ANIM_LOOP);
}

CTEST(anim_timing, single_frame_png)
{
	check_file("data/png/rgb8_srgb.png", 1, 0u, 0);
}

/*
 * Synthetic GIF: w x h logical screen, 2-colour global table, frame_count 1x1 frames at (0,0).
 * Each frame is 2C <pos/size> 00 + LZW min code size 2, one 2-byte sub-block (clear, index 0, EOI).
 * Returns the length written to buf (cap must be >= 20 + 15 * frame_count).
 */
static size_t build_gif(uint8_t *buf, size_t cap, uint16_t w, uint16_t h, int frame_count)
{
	static const uint8_t head[] = { 'G', 'I', 'F', '8', '9', 'a' };
	static const uint8_t frame[] = { 0x2C, 0, 0, 0, 0, 1, 0, 1, 0, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00 };
	size_t n = 0;
	if (cap < 20 + sizeof(frame) * (size_t)frame_count) {
		return 0;
	}
	memcpy(buf, head, sizeof(head));
	n = sizeof(head);
	buf[n++] = (uint8_t)(w & 0xFF);
	buf[n++] = (uint8_t)(w >> 8);
	buf[n++] = (uint8_t)(h & 0xFF);
	buf[n++] = (uint8_t)(h >> 8);
	buf[n++] = 0x80; /* global colour table, 2 entries */
	buf[n++] = 0;    /* background */
	buf[n++] = 0;    /* aspect */
	static const uint8_t gct[] = { 0, 0, 0, 0xFF, 0xFF, 0xFF };
	memcpy(buf + n, gct, sizeof(gct));
	n += sizeof(gct);
	for (int i = 0; i < frame_count; i++) {
		memcpy(buf + n, frame, sizeof(frame));
		n += sizeof(frame);
	}
	buf[n++] = 0x3B;
	return n;
}

CTEST(anim_timing, gif_synthetic_decodes)
{
	if (!img2_can_decode(IMG2_MIME_GIF)) {
		return; /* skipped: GIF decoder not built */
	}
	/* sanity check of build_gif: a small canvas decodes (frames composed onto the 4x3 canvas) */
	uint8_t buf[256];
	size_t len = build_gif(buf, sizeof(buf), 4, 3, 2);
	ASSERT_TRUE(len > 0);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode(buf, len, NULL, &fr));
	ASSERT_EQUAL(2, fr.count);
	ASSERT_EQUAL(4, fr.frames[0]->width);
	ASSERT_EQUAL(3, fr.frames[0]->height);
	img2_frames_free(&fr);
}

CTEST(anim_timing, gif_oversized_screen_is_limit)
{
	if (!img2_can_decode(IMG2_MIME_GIF)) {
		return; /* skipped: GIF decoder not built */
	}
	/* 65535 x 65535 logical screen: canvas over IMG2_MAX_PIXELS -> LIMIT, not NOMEM */
	uint8_t buf[256];
	size_t len = build_gif(buf, sizeof(buf), 0xFFFF, 0xFFFF, 1);
	ASSERT_TRUE(len > 0);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_decode(buf, len, NULL, &fr));
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);
}

CTEST(anim_timing, gif_total_pixels_is_limit)
{
	if (!img2_can_decode(IMG2_MIME_GIF)) {
		return; /* skipped: GIF decoder not built */
	}
	/* [F14] 11 frames of a 10000 x 10000 canvas (each frame within IMG2_MAX_PIXELS) = 1.1e9 > 2^30 total */
	uint8_t buf[512];
	size_t len = build_gif(buf, sizeof(buf), 10000, 10000, 11);
	ASSERT_TRUE(len > 0);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_decode(buf, len, NULL, &fr));
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);
}

CTEST(anim_timing, gif_is_animated_record_walk)
{
	if (!img2_can_decode(IMG2_MIME_GIF)) {
		return; /* skipped: GIF decoder not built */
	}
	/* [F16] img2_is_animated only walks the records */
	uint8_t buf[256];
	size_t len = build_gif(buf, sizeof(buf), 4, 3, 2);
	ASSERT_TRUE(len > 0);
	ASSERT_TRUE(img2_is_animated(buf, len));

	len = build_gif(buf, sizeof(buf), 4, 3, 1);
	ASSERT_TRUE(len > 0);
	ASSERT_FALSE(img2_is_animated(buf, len));

	/* Raster data is skipped, not LZW-decoded: undefined LZW codes (0xFF 0xFF = code 7 with no
	 * prior entry) make the decode fail, but the probe still sees two frames */
	len = build_gif(buf, sizeof(buf), 4, 3, 2);
	ASSERT_TRUE(len > 0);
	for (size_t i = 0; i + 15 <= len; i++) {
		if (buf[i] == 0x2C && buf[i + 10] == 0x02 && buf[i + 11] == 0x02) {
			buf[i + 12] = 0xFF;
			buf[i + 13] = 0xFF;
		}
	}
	img2_frames_t fr;
	ASSERT_NOT_EQUAL(IMG2_OK, img2_decode(buf, len, NULL, &fr));
	ASSERT_TRUE(img2_is_animated(buf, len));

	/* Truncated after the first frame's raster (no 2nd descriptor, no trailer) -> false */
	len = build_gif(buf, sizeof(buf), 4, 3, 2);
	ASSERT_FALSE(img2_is_animated(buf, len - 16));
}

CTEST(anim_timing, max_frames_keeps_loop_count)
{
	if (!img2_can_decode(IMG2_MIME_GIF)) {
		return; /* skipped: GIF decoder not built */
	}
	size_t len = 0;
	uint8_t *data = load_file("data/anim/smoke_anim.gif", &len);
	ASSERT_NOT_NULL(data);
	img2_frames_t fr;
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	opts.max_frames = 2;
	ASSERT_EQUAL(IMG2_OK, img2_decode(data, len, &opts, &fr));
	ASSERT_EQUAL(2, fr.count);
	ASSERT_EQUAL(ANIM_LOOP, fr.loop_count);
	ASSERT_EQUAL(ANIM_DELAY_MS, fr.frames[1]->delay_ms);
	img2_frames_free(&fr);
	free(data);
}
