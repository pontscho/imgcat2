/**
 * @file anim_test_util.h
 * @brief Shared helpers of the animated-format tests (test_animation_timing / _jxl / _webp / _heif, S13b)
 *
 * Public API only (<img2/img2.h>) plus ctest.h; header-only (static functions).
 */

#ifndef IMG2_TESTS_ANIM_TEST_UTIL_H
#define IMG2_TESTS_ANIM_TEST_UTIL_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../ctest.h"

/** Growable output buffer for the in-test encoders */
typedef struct {
	uint8_t *data;
	size_t len;
	size_t cap;
	int failed;
} anim_buf_t;

static inline void anim_buf_put(anim_buf_t *b, const void *src, size_t n)
{
	if (b->failed) {
		return;
	}
	if (b->len + n > b->cap) {
		size_t cap = b->cap ? b->cap : 256;
		while (cap < b->len + n) {
			cap *= 2;
		}
		uint8_t *p = (uint8_t *)realloc(b->data, cap);
		if (p == NULL) {
			b->failed = 1;
			return;
		}
		b->data = p;
		b->cap = cap;
	}
	memcpy(b->data + b->len, src, n);
	b->len += n;
}

static inline void anim_buf_free(anim_buf_t *b)
{
	free(b->data);
	memset(b, 0, sizeof(*b));
}

/** img2_decode with default options; checks count, loop count and the delay of every frame (delays NULL = all 0) */
static inline void anim_check_frames(const uint8_t *data, size_t len, int count, const uint32_t *delays, int loop_count)
{
	img2_frames_t fr;
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	ASSERT_EQUAL(IMG2_OK, img2_decode(data, len, &opts, &fr));
	ASSERT_EQUAL(count, fr.count);
	ASSERT_EQUAL(loop_count, fr.loop_count);
	for (int i = 0; i < fr.count; i++) {
		ASSERT_NOT_NULL(fr.frames[i]);
		ASSERT_EQUAL_U(delays != NULL ? delays[i] : 0u, fr.frames[i]->delay_ms);
	}
	img2_frames_free(&fr);
}

/** Same with one delay for every frame */
static inline void anim_check_uniform(const uint8_t *data, size_t len, int count, uint32_t delay_ms, int loop_count)
{
	img2_frames_t fr;
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	ASSERT_EQUAL(IMG2_OK, img2_decode(data, len, &opts, &fr));
	ASSERT_EQUAL(count, fr.count);
	ASSERT_EQUAL(loop_count, fr.loop_count);
	for (int i = 0; i < fr.count; i++) {
		ASSERT_NOT_NULL(fr.frames[i]);
		ASSERT_EQUAL_U(delay_ms, fr.frames[i]->delay_ms);
	}
	img2_frames_free(&fr);
}

/** img2_decode must fail with `expected` and leave *out zeroed */
static inline void anim_check_status(const uint8_t *data, size_t len, img2_status_t expected)
{
	img2_frames_t fr;
	ASSERT_EQUAL(expected, img2_decode(data, len, NULL, &fr));
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);
}

#endif /* IMG2_TESTS_ANIM_TEST_UTIL_H */
