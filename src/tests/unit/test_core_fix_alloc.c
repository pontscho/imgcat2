/**
 * @file test_core_fix_alloc.c
 * @brief stb_image images with a zero dimension (decoder_stb.c) and the TIFF multipage path that counts no
 *        directories (decoder_tiff.c), with the allocation harness
 *
 * Linked with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=strdup,--wrap=free (alloc_fail.h with
 * AF_WRAP_FREE, Linux only): af_live() counts the blocks allocated through the wrappers and not yet freed, and
 * af_arm2() fails two allocations of one decode.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../../libimg2/decoders/decoder.h"
#define AF_WRAP_FREE
#include "alloc_fail.h"
#include "test_cov_b_util.h"

/* ===== stb_image: width or height 0 ===== */

/** 40-byte uncompressed RGB PSD of h x w (no mode data, resources or layers, no pixel bytes) */
static void psd_empty(cb_buf_t *b, uint32_t h, uint32_t w)
{
	cb_str(b, "8BPS");
	cb_be16(b, 1);                  /* version */
	for (int i = 0; i < 6; i++) {   /* reserved */
		cb_u8(b, 0);
	}
	cb_be16(b, 3);                  /* channels */
	cb_be32(b, h);
	cb_be32(b, w);
	cb_be16(b, 8);                  /* depth */
	cb_be16(b, 3);                  /* colour mode RGB */
	cb_be32(b, 0);                  /* mode data length */
	cb_be32(b, 0);                  /* image resources length */
	cb_be32(b, 0);                  /* layer and mask length */
	cb_be16(b, 0);                  /* raw data */
}

static char s_last_err[256];

static void capture_log(img2_log_level_t level, const char *msg, void *user)
{
	(void)user;
	if (level == IMG2_LOG_ERROR && strstr(msg, "stb_image returned") != NULL) {
		snprintf(s_last_err, sizeof(s_last_err), "%s", msg);
	}
}

/* decode through the dispatcher: IMG2_E_CODEC, *out zeroed, stb_image's malloc(0) buffer freed */
static void expect_zero_dimension(const uint8_t *data, size_t len, const char *dims)
{
	img2_frames_t fr;
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	img2_set_log_callback(capture_log, NULL);
	/* warm-up: anything allocated once per process (log state) is not a leak */
	img2_decode(data, len, &opts, &fr);
	img2_frames_free(&fr);

	s_last_err[0] = '\0';
	long before = af_live();
	img2_status_t st = img2_decode(data, len, &opts, &fr);
	long after = af_live();
	img2_set_log_callback(NULL, NULL);
	ASSERT_EQUAL(IMG2_E_CODEC, st);
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);
	ASSERT_EQUAL(0, after - before);
	char want[96];
	snprintf(want, sizeof(want), "invalid dimensions: %s", dims);
	ASSERT_NOT_NULL(strstr(s_last_err, want));
}

CTEST(core_fix_stb, psd_zero_width_and_height)
{
	/* stb_image's PSD loader accepts 0 x 0 (mad3sizes_valid passes 0) and returns malloc(0) */
	cb_buf_t b = { 0 };
	psd_empty(&b, 0, 0);
	ASSERT_EQUAL(40, (int)b.n);
	expect_zero_dimension(b.d, b.n, "0x0");
	cb_free(&b);
}

CTEST(core_fix_stb, psd_zero_height)
{
	/* width 1, height 0: the second operand of the dimension check */
	cb_buf_t b = { 0 };
	psd_empty(&b, 0, 1);
	expect_zero_dimension(b.d, b.n, "1x0");
	cb_free(&b);
}

CTEST(core_fix_stb, hdr_zero_size)
{
	/* Radiance HDR "-Y 0 +X 0": a malloc(0) float buffer, converted to a malloc(0) 8-bit buffer */
	static const char hdr[] = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 0 +X 0\n";
	expect_zero_dimension((const uint8_t *)hdr, sizeof(hdr) - 1, "0x0");
}

/* ===== TIFF: the multipage path counts no directories ===== */

#ifdef IMG2_HAVE_TIFF
/* Classic TIFF, RGB 8-bit, one 2x2 strip per page (as in test_cov_b_alloc.c) */
static void tiff_two_pages(cb_buf_t *b)
{
	cb_str(b, "II");
	cb_le16(b, 42);
	cb_le32(b, 8);
	for (int p = 0; p < 2; p++) {
		uint32_t ifd = (uint32_t)b->n;
		uint32_t data = ifd + 2 + 10 * 12 + 4;
		uint32_t bps = data + 12;
		uint32_t next = p == 0 ? bps + 6 + 2 : 0;
		cb_le16(b, 10);
		const uint32_t e[10][4] = {
			{ 256, 3, 1, 2 }, { 257, 3, 1, 2 }, { 258, 3, 3, bps }, { 259, 3, 1, 1 }, { 262, 3, 1, 2 },
			{ 273, 4, 1, data }, { 277, 3, 1, 3 }, { 278, 3, 1, 2 }, { 279, 4, 1, 12 }, { 284, 3, 1, 1 },
		};
		for (int i = 0; i < 10; i++) {
			cb_le16(b, e[i][0]);
			cb_le16(b, e[i][1]);
			cb_le32(b, e[i][2]);
			cb_le32(b, e[i][3]);
		}
		cb_le32(b, next);
		for (int i = 0; i < 12; i++) {
			cb_u8(b, (unsigned)(p * 40 + i));
		}
		cb_le16(b, 8);
		cb_le16(b, 8);
		cb_le16(b, 8);
		cb_le16(b, 0);
	}
}

static int s_no_pages; /* "TIFF has no pages" logged */

static void tiff_log_probe(img2_log_level_t level, const char *msg, void *user)
{
	(void)level;
	(void)user;
	if (strstr(msg, "TIFF has no pages") != NULL) {
		s_no_pages++;
	}
}

CTEST(core_fix_tiff, multipage_with_no_directories)
{
	/* TIFFNumberOfDirectories() returns 0 for a 2-page file when two allocations fail: the directory hash set
	 * of the second TIFFClientOpen (decode_tiff_multipage), so directory 0 is not registered while it is
	 * read, then the allocation that registers it while the directories are counted
	 * (_TIFFCheckDirNumberAndOffset). One failure cannot do it: directory 0 is then registered either while
	 * it is read or, without a failure, while it is counted. */
	cb_buf_t b = { 0 };
	tiff_two_pages(&b);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, img2_decode_tiff(b.d, b.n, NULL, &fr));
	ASSERT_EQUAL(2, fr.count);
	img2_frames_free(&fr);

	af_arm(0x7fffffffL);
	ASSERT_EQUAL(IMG2_OK, img2_decode_tiff(b.d, b.n, NULL, &fr));
	long total = af_calls();
	af_disarm();
	img2_frames_free(&fr);
	ASSERT_TRUE(total > 10);

	long hit1 = -1, hit2 = -1;
	int bad = 0;
	img2_set_log_callback(tiff_log_probe, NULL);
	for (long n1 = 0; n1 < total && hit1 < 0; n1++) {
		for (long n2 = n1 + 1; n2 < total && hit1 < 0; n2++) {
			s_no_pages = 0;
			af_arm2(n1, n2);
			img2_status_t st = img2_decode_tiff(b.d, b.n, NULL, &fr);
			int fires = af_fire_count();
			af_disarm();
			if (st == IMG2_OK) {
				bad += fr.count < 1 || fr.frames == NULL;
			} else {
				bad += fr.count != 0 || fr.frames != NULL;
			}
			img2_frames_free(&fr);
			if (s_no_pages > 0) {
				bad += st != IMG2_E_CODEC || fires != 2;
				hit1 = n1;
				hit2 = n2;
			}
			if (fires < 2) {
				break; /* the decode ends before allocation n2: larger n2 change nothing */
			}
		}
	}
	img2_set_log_callback(NULL, NULL);
	ASSERT_EQUAL(0, bad);
	ASSERT_TRUE(hit1 >= 0);
	printf("  (TIFF has no pages with allocations %ld and %ld of %ld failed)\n", hit1, hit2, total);
	cb_free(&b);
}
#endif
