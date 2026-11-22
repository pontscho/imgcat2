/**
 * @file test_heif_fix_edges.c
 * @brief HEIF/AVIF decoder edge cases from the coverage-exclusion audit (no codec plugins involved)
 *
 * White-box: img2_decode_heif(), img2_decode_avif() and img2_heif_decode_sequence() are called directly on
 * fixtures built in-test with libheif's uncompressed ('unci') codec and then byte-patched:
 * - an 'ispe' larger than INT_MAX passes libheif's reader, and the handle then reports a size of 0;
 * - an 'ispe' of 40000 x 40000 (one image alone above IMG2_MAX_TOTAL_PIXELS) is rejected before any decode;
 * - a movie duration shorter than the track duration makes libheif's repeat edit list yield zero samples.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libheif/heif.h>
#include <libheif/heif_sequences.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/decoders/decoder.h"
#include "../ctest.h"
#include "cov_c_heif_util.h"
#include "cov_c_util.h"

typedef img2_status_t (*hd_fn_t)(const uint8_t *, size_t, const img2_decode_opts_t *, img2_frames_t *);

static const hd_fn_t k_decoders[2] = { img2_decode_heif, img2_decode_avif };

/* Both decoders must return `st` (and `count` frames on success) */
static void hd_expect(const uint8_t *data, size_t len, img2_status_t st, int count)
{
	for (int k = 0; k < 2; k++) {
		img2_frames_t fr = { 0 };
		ASSERT_EQUAL(st, k_decoders[k](data, len, NULL, &fr));
		ASSERT_EQUAL(st == IMG2_OK ? count : 0, fr.count);
		img2_frames_free(&fr);
	}
}

static long find4(const uint8_t *d, size_t len, const char *cc, size_t from)
{
	for (size_t i = from; i + 4 <= len; i++) {
		if (memcmp(d + i, cc, 4) == 0) {
			return (long)i;
		}
	}
	return -1;
}

/* every 'ispe' box: width x height; returns the number of boxes patched */
static int patch_ispe(uint8_t *d, size_t len, uint32_t w, uint32_t h)
{
	int n = 0;
	for (long p = find4(d, len, "ispe", 0); p >= 0; p = find4(d, len, "ispe", (size_t)p + 4)) {
		cov_put_be32(d + p + 8, w);
		cov_put_be32(d + p + 12, h);
		n++;
	}
	return n;
}

/* ===== 'ispe' beyond INT_MAX: the handle reports 0, the pre-check skips it, the decode fails ===== */

CTEST(heif_fix_edges, list_ispe_above_int_max)
{
	heif_init(NULL);
	size_t len = 0;
	uint8_t *d = hb_list(heif_compression_uncompressed, 2, 16, 16, NULL, 0, &len);
	ASSERT_NOT_NULL(d);
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_LIST, img2_heif_layout(d, len, NULL));
	uint8_t *c = malloc(len);
	ASSERT_NOT_NULL(c);

	/* width 0x80000000: heif_image_handle_get_width() == 0 (the width > 0 test of the pre-check is false) */
	memcpy(c, d, len);
	ASSERT_TRUE(patch_ispe(c, len, 0x80000000u, 16) > 0);
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_LIST, img2_heif_layout(c, len, NULL));
	hd_expect(c, len, IMG2_E_CODEC, 0);

	/* height 0x80000000: the height > 0 test is false */
	memcpy(c, d, len);
	ASSERT_TRUE(patch_ispe(c, len, 16, 0x80000000u) > 0);
	hd_expect(c, len, IMG2_E_CODEC, 0);

	free(c);
	free(d);
	heif_deinit();
}

/* ===== one image alone above IMG2_MAX_TOTAL_PIXELS: LIMIT before anything is decoded ===== */

CTEST(heif_fix_edges, list_single_image_above_total_limit)
{
	heif_init(NULL);
	size_t len = 0;
	uint8_t *d = hb_list(heif_compression_uncompressed, 2, 16, 16, NULL, 0, &len);
	ASSERT_NOT_NULL(d);
	/* 40000 x 40000 = 1.6e9 pixels > 2^30: img2_decode_add_frame_pixels() rejects the first handle by itself */
	ASSERT_TRUE(patch_ispe(d, len, 40000, 40000) > 0);
	ASSERT_EQUAL(IMG2_HEIF_LAYOUT_LIST, img2_heif_layout(d, len, NULL));
	hd_expect(d, len, IMG2_E_LIMIT, 0);
	free(d);
	heif_deinit();
}

/* ===== sequence whose repeat edit list yields no sample ===== */

CTEST(heif_fix_edges, sequence_without_output_samples)
{
	heif_init(NULL);
	/* track and movie timescale 1000, 2 samples of 100 ticks, repeat edit list (3 repetitions) */
	hb_seq_t p = { heif_compression_uncompressed, 2, 1000, 3, 100, 0 };
	size_t len = 0;
	uint8_t *d = hb_seq(&p, &len);
	ASSERT_NOT_NULL(d);

	int fc = 0;
	int lc = 0;
	img2_status_t st = IMG2_OK;
	img2_image_t **frames = img2_heif_decode_sequence(d, len, "HEIF", &fc, &lc, &st);
	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(2, fc);
	for (int i = 0; i < fc; i++) {
		img2_image_destroy(frames[i]);
	}
	free(frames);

	/* movie duration 1 tick < track duration 200: libheif's repeat multiplier is 0, so the very first
	 * heif_track_decode_next_image() reports End_of_sequence */
	long m = find4(d, len, "mvhd", 0);
	ASSERT_TRUE(m > 0);
	if (d[m + 4] == 1) {
		memset(d + m + 28, 0, 8);
		d[m + 35] = 1;
	} else {
		cov_put_be32(d + m + 20, 1);
	}
	st = IMG2_OK;
	fc = 7;
	ASSERT_NULL(img2_heif_decode_sequence(d, len, "HEIF", &fc, &lc, &st));
	ASSERT_EQUAL(IMG2_E_CODEC, st);
	ASSERT_EQUAL(0, fc);
	st = IMG2_OK;
	ASSERT_NULL(img2_heif_decode_sequence(d, len, "AVIF", &fc, &lc, &st));
	ASSERT_EQUAL(IMG2_E_CODEC, st);
	free(d);
	heif_deinit();
}
