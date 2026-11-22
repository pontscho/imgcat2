/**
 * @file test_animation_heif.c
 * @brief Multi-image HEIF / AVIF (S13b) and HEIF / AVIF image sequences (S13c) through img2_decode
 *
 * Fixtures are encoded in-test with the vendored libheif (kvazaar for HEVC, aom for AV1):
 * - lists: N tiny images added to one context with heif_context_encode_image, i.e. N top-level images
 *   (not a sequence track). The decoders return them as N frames without timing.
 * - sequences: one visual sequence track (heif_context_add_visual_sequence_track +
 *   heif_track_encode_sequence_image; ftyp avis / hevc), no top-level image. The decoders return the
 *   samples as frames with their durations and the track's repetition count.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libheif/heif.h>

#if LIBHEIF_HAVE_VERSION(1, 23, 0)
#include <libheif/heif_sequences.h>
#define HEIF_TEST_SEQUENCES 1
#else
#define HEIF_TEST_SEQUENCES 0
#endif

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

#if HEIF_TEST_SEQUENCES

/* Sequence fixture parameters */
typedef struct {
	uint32_t track_timescale;  /* 0 = libheif default track timescale (90000), != movie timescale 1000 */
	uint32_t repetitions;      /* heif_context_set_number_of_sequence_repetitions (0 = infinite) */
	const uint32_t *durations; /* ms per frame, NULL = 10 ms each */
	int count;
	int distinct;              /* fill frame i with grey 40 + 80 * i (i < 3) instead of 128 */
} heif_seq_params_t;

/*
 * One visual image-sequence track (no top-level image), HEIF_TEST_SIZE^2 frames, movie timescale 1000.
 * Returns a malloc'ed buffer (*len set) or NULL; *no_encoder = 1 when libheif has no such encoder.
 */
static uint8_t *heif_build_sequence(enum heif_compression_format compression, const heif_seq_params_t *p, size_t *len, int *no_encoder)
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
		heif_encoder_set_parameter(enc, "speed", "9");
	}

	heif_context_set_sequence_timescale(ctx, 1000);
	heif_context_set_number_of_sequence_repetitions(ctx, p->repetitions);

	heif_track_options *topts = NULL;
	if (p->track_timescale != 0) {
		topts = heif_track_options_alloc();
		if (topts != NULL) {
			heif_track_options_set_timescale(topts, p->track_timescale);
		}
	}
	heif_track *track = NULL;
	err = heif_context_add_visual_sequence_track(ctx, HEIF_TEST_SIZE, HEIF_TEST_SIZE, heif_track_type_image_sequence, topts, NULL, &track);
	if (topts != NULL) {
		heif_track_options_release(topts);
	}
	int ok = err.code == heif_error_Ok && track != NULL;
	uint32_t ticks_per_s = ok ? heif_track_get_timescale(track) : 0;

	for (int i = 0; i < p->count && ok; i++) {
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
		int grey = (p->distinct && i < 3) ? 40 + 80 * i : 128;
		for (int y = 0; y < HEIF_TEST_SIZE; y++) {
			memset(plane + (size_t)y * (size_t)stride, grey, HEIF_TEST_SIZE * 3);
		}
		uint32_t ms = p->durations != NULL ? p->durations[i] : 10u;
		heif_image_set_duration(img, (uint32_t)((uint64_t)ms * ticks_per_s / 1000u));
		err = heif_track_encode_sequence_image(track, img, enc, NULL);
		heif_image_release(img);
		if (err.code != heif_error_Ok) {
			fprintf(stderr, "heif_track_encode_sequence_image: %s\n", err.message);
			ok = 0;
		}
	}
	/* No explicit heif_track_encode_end_of_sequence: libheif 1.23.7 does not forget the encoder after it,
	 * so heif_context_write flushes again through the (then released) encoder. Let the write end the
	 * sequence and release the encoder afterwards. */
	anim_buf_t b = { 0 };
	if (ok) {
		struct heif_writer writer;
		writer.writer_api_version = 1;
		writer.write = heif_buf_write;
		err = heif_context_write(ctx, &writer, &b);
		ok = err.code == heif_error_Ok && !b.failed;
	}
	heif_track_release(track);
	heif_encoder_release(enc);
	heif_context_free(ctx);
	if (!ok) {
		anim_buf_free(&b);
		return NULL;
	}
	*len = b.len;
	return b.data;
}

static const uint32_t k_seq_delays[3] = { 50u, 100u, 150u };

/* 3 frames 50/100/150 ms, track timescale == movie timescale, played 3 times */
static void check_heif_sequence(enum heif_compression_format compression, img2_mime_t mime)
{
	ASSERT_TRUE(img2_can_decode(mime));
	heif_seq_params_t p = { 1000u, 3u, k_seq_delays, 3, 1 };
	size_t len = 0;
	int no_encoder = 0;
	uint8_t *data = heif_build_sequence(compression, &p, &len, &no_encoder);
	if (data == NULL && no_encoder) {
		fprintf(stderr, "skipped: libheif has no encoder for compression %d\n", (int)compression);
		return;
	}
	ASSERT_NOT_NULL(data);
	ASSERT_EQUAL(mime, img2_detect_mime(data, len));
	ASSERT_TRUE(img2_is_animated(data, len));

	/* 3 samples -> 3 frames (the edit-list repetition is NOT unrolled into 9 frames), loop 3 */
	anim_check_frames(data, len, 3, k_seq_delays, 3);

	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode(data, len, NULL, &fr));
	ASSERT_EQUAL(3, fr.count);
	for (int i = 0; i < 3; i++) {
		ASSERT_EQUAL(HEIF_TEST_SIZE, fr.frames[i]->width);
		ASSERT_EQUAL(HEIF_TEST_SIZE, fr.frames[i]->height);
		ASSERT_INTERVAL(40 + 80 * i - 16, 40 + 80 * i + 16, fr.frames[i]->pixels[0]); /* sample order */
	}
	img2_frames_free(&fr);

	/* max_frames trims, loop_count survives */
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	opts.max_frames = 2;
	ASSERT_EQUAL(IMG2_OK, img2_decode(data, len, &opts, &fr));
	ASSERT_EQUAL(2, fr.count);
	ASSERT_EQUAL(3, fr.loop_count);
	ASSERT_EQUAL_U(100u, fr.frames[1]->delay_ms);
	img2_frames_free(&fr);
	free(data);

	/* one-sample sequence: not animated, 1 frame without timing */
	heif_seq_params_t one = { 1000u, 3u, k_seq_delays, 1, 1 };
	data = heif_build_sequence(compression, &one, &len, &no_encoder);
	ASSERT_NOT_NULL(data);
	ASSERT_FALSE(img2_is_animated(data, len));
	anim_check_uniform(data, len, 1, 0u, 0);
	free(data);
}

CTEST(anim_heif, heic_sequence_timing)
{
	heif_init(NULL);
	check_heif_sequence(heif_compression_HEVC, IMG2_MIME_HEIF);
	heif_deinit();
}

CTEST(anim_heif, avif_sequence_timing)
{
	heif_init(NULL);
	check_heif_sequence(heif_compression_AV1, IMG2_MIME_AVIF);
	heif_deinit();
}

/* loop_count mapping of heif_track_get_number_of_repetitions; delays always use the TRACK timescale */
CTEST(anim_heif, sequence_loop_count)
{
	heif_init(NULL);
	size_t len = 0;
	int no_encoder = 0;

	/* repetitions 0 = infinite -> loop 0 */
	heif_seq_params_t inf = { 1000u, 0u, k_seq_delays, 3, 0 };
	uint8_t *data = heif_build_sequence(heif_compression_HEVC, &inf, &len, &no_encoder);
	if (data == NULL && no_encoder) {
		fprintf(stderr, "skipped: libheif has no HEVC encoder\n");
		heif_deinit();
		return;
	}
	ASSERT_NOT_NULL(data);
	anim_check_frames(data, len, 3, k_seq_delays, 0);
	free(data);

	/* played once -> loop 1 */
	heif_seq_params_t once = { 1000u, 1u, k_seq_delays, 3, 0 };
	data = heif_build_sequence(heif_compression_HEVC, &once, &len, &no_encoder);
	ASSERT_NOT_NULL(data);
	anim_check_frames(data, len, 3, k_seq_delays, 1);
	free(data);

	/* libheif limitation: with the default track timescale (90000 != movie timescale 1000) libheif does not
	 * interpret the edit list and reports 0 repetitions -> loop 1 (play once, as libheif advises); the
	 * delays still come out right (90000-tick durations) */
	heif_seq_params_t mismatch = { 0u, 3u, k_seq_delays, 3, 0 };
	data = heif_build_sequence(heif_compression_HEVC, &mismatch, &len, &no_encoder);
	ASSERT_NOT_NULL(data);
	anim_check_frames(data, len, 3, k_seq_delays, 1);
	free(data);
	heif_deinit();
}

/* > 200 samples: the first 200 frames, WARN, no error */
CTEST(anim_heif, sequence_frame_cap)
{
	heif_init(NULL);
	heif_seq_params_t p = { 1000u, 2u, NULL, 205, 0 };
	size_t len = 0;
	int no_encoder = 0;
	uint8_t *data = heif_build_sequence(heif_compression_HEVC, &p, &len, &no_encoder);
	if (data == NULL && no_encoder) {
		fprintf(stderr, "skipped: libheif has no HEVC encoder\n");
		heif_deinit();
		return;
	}
	ASSERT_NOT_NULL(data);
	ASSERT_TRUE(img2_is_animated(data, len));
	anim_check_uniform(data, len, 200, 10u, 2);
	free(data);

	/* exactly 200 samples: all of them */
	p.count = 200;
	data = heif_build_sequence(heif_compression_HEVC, &p, &len, &no_encoder);
	ASSERT_NOT_NULL(data);
	anim_check_uniform(data, len, 200, 10u, 2);
	free(data);
	heif_deinit();
}

#endif /* HEIF_TEST_SEQUENCES */
