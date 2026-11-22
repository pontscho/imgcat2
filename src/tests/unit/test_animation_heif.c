/**
 * @file test_animation_heif.c
 * @brief Multi-image HEIF / AVIF through img2_decode (S13b)
 *
 * Fixtures are encoded in-test with the vendored libheif (kvazaar for HEVC, aom for AV1): N tiny images
 * added to one context with heif_context_encode_image, i.e. N top-level images (not a sequence track).
 * The decoders return them as N frames without timing.
 *
 * S13c (libheif sequence/track API) changes the timing: update HEIF_LIST_DELAY_MS / HEIF_LIST_LOOP below
 * if top-level image lists get a different convention, and add track-based fixtures there.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libheif/heif.h>

#include "anim_test_util.h"

/* Today's convention for top-level image lists: no timing (delay 0, loop 0) */
#define HEIF_LIST_DELAY_MS 0u
#define HEIF_LIST_LOOP 0

#define HEIF_TEST_SIZE 16

static struct heif_error heif_buf_write(struct heif_context *ctx, const void *data, size_t size, void *userdata)
{
	(void)ctx;
	anim_buf_t *b = (anim_buf_t *)userdata;
	anim_buf_put(b, data, size);
	struct heif_error err = { b->failed ? heif_error_Memory_allocation_error : heif_error_Ok, heif_suberror_Unspecified, "" };
	return err;
}

/*
 * count images of HEIF_TEST_SIZE^2, image i filled with grey level 40 + 80 * i, encoded with `compression`.
 * Returns a malloc'ed buffer (*len set) or NULL; *no_encoder = 1 when libheif has no such encoder.
 */
static uint8_t *heif_build_list(enum heif_compression_format compression, int count, size_t *len, int *no_encoder)
{
	*no_encoder = 0;
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		return NULL;
	}
	struct heif_encoder *enc = NULL;
	struct heif_error err = heif_context_get_encoder_for_format(ctx, compression, &enc);
	if (err.code != heif_error_Ok || enc == NULL) {
		*no_encoder = 1;
		heif_context_free(ctx);
		return NULL;
	}
	heif_encoder_set_lossy_quality(enc, 50);
	if (compression == heif_compression_AV1) {
		heif_encoder_set_parameter(enc, "speed", "9"); /* fastest aom preset; tiny images */
	}

	anim_buf_t b = { 0 };
	int ok = 1;
	for (int i = 0; i < count && ok; i++) {
		struct heif_image *img = NULL;
		err = heif_image_create(HEIF_TEST_SIZE, HEIF_TEST_SIZE, heif_colorspace_RGB, heif_chroma_interleaved_RGB, &img);
		if (err.code != heif_error_Ok) {
			ok = 0;
			break;
		}
		err = heif_image_add_plane(img, heif_channel_interleaved, HEIF_TEST_SIZE, HEIF_TEST_SIZE, 8);
		int stride = 0;
		uint8_t *plane = err.code == heif_error_Ok ? heif_image_get_plane(img, heif_channel_interleaved, &stride) : NULL;
		if (plane == NULL) {
			heif_image_release(img);
			ok = 0;
			break;
		}
		for (int y = 0; y < HEIF_TEST_SIZE; y++) {
			memset(plane + (size_t)y * (size_t)stride, 40 + 80 * i, HEIF_TEST_SIZE * 3);
		}
		err = heif_context_encode_image(ctx, img, enc, NULL, NULL);
		heif_image_release(img);
		if (err.code != heif_error_Ok) {
			fprintf(stderr, "heif_context_encode_image: %s\n", err.message);
			ok = 0;
		}
	}
	heif_encoder_release(enc);
	if (ok) {
		struct heif_writer writer;
		writer.writer_api_version = 1;
		writer.write = heif_buf_write;
		err = heif_context_write(ctx, &writer, &b);
		ok = err.code == heif_error_Ok && !b.failed;
	}
	heif_context_free(ctx);
	if (!ok) {
		anim_buf_free(&b);
		return NULL;
	}
	*len = b.len;
	return b.data;
}

static void check_heif_list(enum heif_compression_format compression, img2_mime_t mime)
{
	ASSERT_TRUE(img2_can_decode(mime));
	size_t len = 0;
	int no_encoder = 0;
	uint8_t *data = heif_build_list(compression, 3, &len, &no_encoder);
	if (data == NULL && no_encoder) {
		fprintf(stderr, "skipped: libheif has no encoder for compression %d\n", (int)compression);
		return;
	}
	ASSERT_NOT_NULL(data);
	ASSERT_EQUAL(mime, img2_detect_mime(data, len));
	ASSERT_TRUE(img2_is_animated(data, len));

	/* 3 top-level images -> 3 frames, no timing */
	anim_check_uniform(data, len, 3, HEIF_LIST_DELAY_MS, HEIF_LIST_LOOP);

	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode(data, len, NULL, &fr));
	ASSERT_EQUAL(3, fr.count);
	for (int i = 0; i < 3; i++) {
		ASSERT_EQUAL(HEIF_TEST_SIZE, fr.frames[i]->width);
		ASSERT_EQUAL(HEIF_TEST_SIZE, fr.frames[i]->height);
		/* lossy, flat grey: within a generous tolerance of 40 + 80 * i, in list order */
		ASSERT_INTERVAL(40 + 80 * i - 12, 40 + 80 * i + 12, fr.frames[i]->pixels[0]);
	}
	img2_frames_free(&fr);

	/* max_frames is honoured (dispatcher trim; the HEIF/AVIF decoders ignore opts) */
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	opts.max_frames = 2;
	ASSERT_EQUAL(IMG2_OK, img2_decode(data, len, &opts, &fr));
	ASSERT_EQUAL(2, fr.count);
	ASSERT_EQUAL(HEIF_LIST_LOOP, fr.loop_count);
	img2_frames_free(&fr);
	free(data);

	/* a single image is not animated: 1 frame */
	data = heif_build_list(compression, 1, &len, &no_encoder);
	ASSERT_NOT_NULL(data);
	ASSERT_FALSE(img2_is_animated(data, len));
	anim_check_uniform(data, len, 1, 0u, 0);
	free(data);
}

CTEST(anim_heif, heic_image_list_is_frames)
{
	heif_init(NULL);
	check_heif_list(heif_compression_HEVC, IMG2_MIME_HEIF);
	heif_deinit();
}

CTEST(anim_heif, avif_image_list_is_frames)
{
	heif_init(NULL);
	check_heif_list(heif_compression_AV1, IMG2_MIME_AVIF);
	heif_deinit();
}
