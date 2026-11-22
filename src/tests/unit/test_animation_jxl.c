/**
 * @file test_animation_jxl.c
 * @brief Animated JPEG XL through img2_decode (S13b): timing, 200-frame clamp, pixel limits
 *
 * Fixtures are encoded in-test with the vendored libjxl encoder (JxlEncoder, in the fat archive).
 * Frames are tiny; the limit cases use a large canvas with 1x1 cropped frames, so they are rejected from
 * the header walk without ever decoding a full canvas.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jxl/encode.h>

#include "anim_test_util.h"

/*
 * Encode a lossless RGB JXL: xsize x ysize canvas, nframes frames of fw x fh at (0,0) (cropped when smaller
 * than the canvas), frame i filled with value (i * 37) & 0xFF and lasting durations[i] ticks (1 tick = 1 ms;
 * durations NULL = duration_all for every frame). have_animation when nframes > 1 or force_anim.
 * Returns a malloc'ed buffer (*len set) or NULL.
 */
static uint8_t *jxl_encode(uint32_t xsize, uint32_t ysize, uint32_t fw, uint32_t fh, int nframes, const uint32_t *durations, uint32_t duration_all,
                           uint32_t num_loops, size_t *len)
{
	JxlEncoder *enc = JxlEncoderCreate(NULL);
	if (enc == NULL) {
		return NULL;
	}
	uint8_t *out = NULL;
	uint8_t *pix = NULL;

	JxlBasicInfo bi;
	JxlEncoderInitBasicInfo(&bi);
	bi.xsize = xsize;
	bi.ysize = ysize;
	bi.bits_per_sample = 8;
	bi.exponent_bits_per_sample = 0;
	bi.uses_original_profile = JXL_TRUE;
	bi.num_color_channels = 3;
	bi.num_extra_channels = 0;
	bi.alpha_bits = 0;
	if (nframes > 1) {
		bi.have_animation = JXL_TRUE;
		bi.animation.tps_numerator = 1000;
		bi.animation.tps_denominator = 1;
		bi.animation.num_loops = num_loops;
		bi.animation.have_timecodes = JXL_FALSE;
	}
	if (JxlEncoderSetBasicInfo(enc, &bi) != JXL_ENC_SUCCESS) {
		goto fail;
	}
	JxlColorEncoding ce;
	JxlColorEncodingSetToSRGB(&ce, JXL_FALSE);
	if (JxlEncoderSetColorEncoding(enc, &ce) != JXL_ENC_SUCCESS) {
		goto fail;
	}
	JxlEncoderFrameSettings *fs = JxlEncoderFrameSettingsCreate(enc, NULL);
	if (fs == NULL || JxlEncoderSetFrameLossless(fs, JXL_TRUE) != JXL_ENC_SUCCESS ||
	    JxlEncoderFrameSettingsSetOption(fs, JXL_ENC_FRAME_SETTING_EFFORT, 1) != JXL_ENC_SUCCESS) {
		goto fail;
	}

	JxlPixelFormat fmt = { .num_channels = 3, .data_type = JXL_TYPE_UINT8, .endianness = JXL_NATIVE_ENDIAN, .align = 0 };
	size_t pix_len = (size_t)fw * fh * 3;
	pix = malloc(pix_len);
	if (pix == NULL) {
		goto fail;
	}
	for (int i = 0; i < nframes; i++) {
		JxlFrameHeader fh_hdr;
		JxlEncoderInitFrameHeader(&fh_hdr);
		fh_hdr.duration = durations != NULL ? durations[i] : duration_all;
		if (fw != xsize || fh != ysize) {
			fh_hdr.layer_info.have_crop = JXL_TRUE;
			fh_hdr.layer_info.crop_x0 = 0;
			fh_hdr.layer_info.crop_y0 = 0;
			fh_hdr.layer_info.xsize = fw;
			fh_hdr.layer_info.ysize = fh;
		}
		if (JxlEncoderSetFrameHeader(fs, &fh_hdr) != JXL_ENC_SUCCESS) {
			goto fail;
		}
		memset(pix, (i * 37) & 0xFF, pix_len);
		if (JxlEncoderAddImageFrame(fs, &fmt, pix, pix_len) != JXL_ENC_SUCCESS) {
			goto fail;
		}
	}
	JxlEncoderCloseInput(enc);

	size_t cap = 4096;
	size_t used = 0;
	out = malloc(cap);
	if (out == NULL) {
		goto fail;
	}
	for (;;) {
		uint8_t *next = out + used;
		size_t avail = cap - used;
		JxlEncoderStatus s = JxlEncoderProcessOutput(enc, &next, &avail);
		used = (size_t)(next - out);
		if (s == JXL_ENC_SUCCESS) {
			break;
		}
		if (s != JXL_ENC_NEED_MORE_OUTPUT) {
			goto fail;
		}
		uint8_t *grown = realloc(out, cap * 2);
		if (grown == NULL) {
			goto fail;
		}
		out = grown;
		cap *= 2;
	}
	free(pix);
	JxlEncoderDestroy(enc);
	*len = used;
	return out;

fail:
	fprintf(stderr, "jxl_encode failed (encoder error %d)\n", (int)JxlEncoderGetError(enc));
	free(pix);
	free(out);
	JxlEncoderDestroy(enc);
	return NULL;
}

CTEST(anim_jxl, three_frames_timing)
{
	/* 3 frames with distinct durations (tps 1000/1 -> ms), played 3 times */
	static const uint32_t durations[3] = { 30, 70, 120 };
	size_t len = 0;
	uint8_t *data = jxl_encode(2, 2, 2, 2, 3, durations, 0, 3, &len);
	ASSERT_NOT_NULL(data);
	anim_check_frames(data, len, 3, durations, 3);

	/* frames decode in order (fill value i * 37) */
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode(data, len, NULL, &fr));
	ASSERT_EQUAL(3, fr.count);
	for (int i = 0; i < 3; i++) {
		ASSERT_EQUAL(2, fr.frames[i]->width);
		ASSERT_EQUAL(2, fr.frames[i]->height);
		ASSERT_EQUAL((i * 37) & 0xFF, fr.frames[i]->pixels[0]);
	}
	img2_frames_free(&fr);
	free(data);

	/* loop forever (num_loops 0) */
	data = jxl_encode(2, 2, 2, 2, 3, durations, 0, 0, &len);
	ASSERT_NOT_NULL(data);
	anim_check_frames(data, len, 3, durations, 0);
	free(data);
}

CTEST(anim_jxl, frame_cap_keeps_200_frames)
{
	/* 230 frames of 10 ms: clamped to the first 200 (was IMG2_E_CODEC before S13b) */
	size_t len = 0;
	uint8_t *data = jxl_encode(1, 1, 1, 1, 230, NULL, 10, 2, &len);
	ASSERT_NOT_NULL(data);
	anim_check_uniform(data, len, 200, 10u, 2);

	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode(data, len, NULL, &fr));
	ASSERT_EQUAL(200, fr.count);
	ASSERT_EQUAL((199 * 37) & 0xFF, fr.frames[199]->pixels[0]);
	img2_frames_free(&fr);
	free(data);

	/* exactly 200 frames: no clamp involved */
	data = jxl_encode(1, 1, 1, 1, 200, NULL, 10, 0, &len);
	ASSERT_NOT_NULL(data);
	anim_check_uniform(data, len, 200, 10u, 0);
	free(data);
}

CTEST(anim_jxl, total_pixels_is_limit)
{
	/* [F14] 17 frames on an 8192 x 8192 canvas (each within IMG2_MAX_PIXELS) = 1.14e9 > 2^30 total:
	 * rejected after the header walk, before any frame is decoded */
	size_t len = 0;
	uint8_t *data = jxl_encode(8192, 8192, 1, 1, 17, NULL, 10, 0, &len);
	ASSERT_NOT_NULL(data);
	anim_check_status(data, len, IMG2_E_LIMIT);
	free(data);
}

CTEST(anim_jxl, oversized_canvas_is_limit)
{
	/* 20000 x 20000 = 4e8 > IMG2_MAX_PIXELS (2e8), within IMG2_MAX_DIMENSION: LIMIT, not NOMEM/CODEC */
	size_t len = 0;
	uint8_t *data = jxl_encode(20000, 20000, 1, 1, 1, NULL, 0, 0, &len);
	ASSERT_NOT_NULL(data);
	anim_check_status(data, len, IMG2_E_LIMIT);
	free(data);
}
