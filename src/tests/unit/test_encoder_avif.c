/**
 * @file test_encoder_avif.c
 * @brief Unit tests for the AVIF encoder (AV1 in HEIF through the shared libheif path)
 *
 * Round trip through img2_encode() / img2_decode() (dimensions, PSNR), alpha preservation, the quality
 * parameter, run-to-run determinism, img2_can_encode() and the encoder table row.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/encoders/encoder.h"
#include "../ctest.h"

/* smooth gradient + a few hard edges, opaque unless with_alpha (left half opaque, right half transparent) */
static img2_image_t *make_image(uint32_t w, uint32_t h, bool with_alpha)
{
	img2_image_t *img = img2_image_alloc(w, h);
	ASSERT_NOT_NULL(img);
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint8_t *p = img2_image_get_pixel(img, x, y);
			p[0] = (uint8_t)(x * 255 / w);
			p[1] = (uint8_t)(y * 255 / h);
			p[2] = ((x / 16 + y / 16) % 2) ? 200 : 60;
			p[3] = (with_alpha && x >= w / 2) ? 0 : 255;
		}
	}
	return img;
}

/* PSNR of channels [c0, c1) over all pixels */
static double psnr(const img2_image_t *a, const img2_image_t *b, int c0, int c1)
{
	double se = 0;
	size_t n = 0;
	for (uint32_t y = 0; y < a->height; y++) {
		const uint8_t *pa = a->pixels + (size_t)y * a->stride;
		const uint8_t *pb = b->pixels + (size_t)y * b->stride;
		for (uint32_t x = 0; x < a->width; x++) {
			for (int c = c0; c < c1; c++) {
				double d = (double)pa[x * 4 + c] - (double)pb[x * 4 + c];
				se += d * d;
				n++;
			}
		}
	}
	return se == 0 ? 99.0 : 10.0 * log10(255.0 * 255.0 / (se / (double)n));
}

static void encode(img2_image_t *img, int quality, uint8_t **buf, size_t *len)
{
	img2_frames_t in = { &img, 1, 0 };
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.quality = quality;
	*buf = NULL;
	*len = 0;
	ASSERT_EQUAL(IMG2_OK, img2_encode(&in, IMG2_FORMAT_AVIF, &eo, buf, len));
	ASSERT_NOT_NULL(*buf);
	ASSERT_TRUE(*len > 0);
}

CTEST(encoder_avif, can_encode_and_table_row)
{
	/* the vendored libheif has aom (AV1) and kvazaar (HEVC) */
	ASSERT_TRUE(img2_can_encode(IMG2_FORMAT_AVIF));
	ASSERT_TRUE(img2_can_encode(IMG2_FORMAT_HEIF));
	ASSERT_TRUE(img2_encode_avif_available());
	ASSERT_TRUE(img2_encode_heif_available());

	bool found = false;
	for (size_t i = 0; i < img2_encoder_count(); i++) {
		const img2_encoder_info_t *info = img2_encoder_info(i);
		if (info->format == IMG2_FORMAT_AVIF) {
			ASSERT_STR(".avif", info->extension);
			ASSERT_STR("AVIF (libheif/aom)", info->name);
			found = true;
		}
	}
	ASSERT_TRUE(found);

	const img2_encoder_entry_t *e = img2_encoder_find(IMG2_FORMAT_AVIF);
	ASSERT_NOT_NULL(e);
	ASSERT_EQUAL(99, e->default_quality);
}

CTEST(encoder_avif, null_inputs)
{
	img2_image_t *img = img2_image_alloc(1, 1);
	ASSERT_NOT_NULL(img);
	uint8_t *out = NULL;
	size_t size = 0;
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode_avif(NULL, 50, &out, &size));
	ASSERT_NULL(out);
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode_avif(img, 50, NULL, &size));
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode_avif(img, 50, &out, NULL));
	img2_image_destroy(img);
}

CTEST(encoder_avif, round_trip)
{
	img2_image_t *img = make_image(96, 64, false);
	uint8_t *buf = NULL;
	size_t len = 0;
	encode(img, -1, &buf, &len); /* registry default */

	ASSERT_EQUAL(IMG2_MIME_AVIF, img2_detect_mime(buf, len));
	img2_frames_t out;
	ASSERT_EQUAL(IMG2_OK, img2_decode(buf, len, NULL, &out));
	ASSERT_EQUAL(1, (int)out.count);
	ASSERT_EQUAL(96, out.frames[0]->width);
	ASSERT_EQUAL(64, out.frames[0]->height);
	double p = psnr(img, out.frames[0], 0, 3);
	ASSERT_TRUE(p > 30.0);
	ASSERT_EQUAL(99.0, psnr(img, out.frames[0], 3, 4)); /* opaque stays opaque */

	img2_frames_free(&out);
	img2_free(buf);
	img2_image_destroy(img);
}

CTEST(encoder_avif, alpha_preserved)
{
	img2_image_t *img = make_image(64, 32, true);
	uint8_t *buf = NULL;
	size_t len = 0;
	encode(img, 80, &buf, &len);

	img2_frames_t out;
	ASSERT_EQUAL(IMG2_OK, img2_decode(buf, len, NULL, &out));
	const img2_image_t *d = out.frames[0];
	ASSERT_EQUAL(64, d->width);
	ASSERT_EQUAL(32, d->height);
	/* away from the edge: the opaque half is (nearly) 255, the transparent half (nearly) 0 */
	for (uint32_t y = 0; y < 32; y++) {
		for (uint32_t x = 0; x < 64; x++) {
			uint8_t a = d->pixels[(size_t)y * d->stride + x * 4 + 3];
			if (x < 28) {
				ASSERT_TRUE(a >= 250);
			} else if (x >= 36) {
				ASSERT_TRUE(a <= 5);
			}
		}
	}
	ASSERT_TRUE(psnr(img, d, 3, 4) > 30.0);

	img2_frames_free(&out);
	img2_free(buf);
	img2_image_destroy(img);
}

CTEST(encoder_avif, quality_levels)
{
	img2_image_t *img = make_image(96, 96, false);
	uint8_t *lo = NULL;
	uint8_t *hi = NULL;
	size_t lo_len = 0;
	size_t hi_len = 0;
	encode(img, 10, &lo, &lo_len);
	encode(img, 90, &hi, &hi_len);
	ASSERT_TRUE(lo_len < hi_len);

	/* out-of-range native qualities are clamped, not rejected */
	uint8_t *buf = NULL;
	size_t len = 0;
	ASSERT_EQUAL(IMG2_OK, img2_encode_avif(img, -5, &buf, &len));
	free(buf);
	ASSERT_EQUAL(IMG2_OK, img2_encode_avif(img, 500, &buf, &len));
	free(buf);

	img2_free(lo);
	img2_free(hi);
	img2_image_destroy(img);
}

/* aom at the libheif defaults is deterministic run to run (the golden AVIF cases rely on it) */
CTEST(encoder_avif, deterministic)
{
	img2_image_t *img = make_image(64, 64, true);
	uint8_t *a = NULL;
	uint8_t *b = NULL;
	size_t a_len = 0;
	size_t b_len = 0;
	encode(img, -1, &a, &a_len);
	encode(img, -1, &b, &b_len);
	ASSERT_DATA(a, a_len, b, b_len);
	img2_free(a);
	img2_free(b);
	img2_image_destroy(img);
}

CTEST(encoder_avif, dispatch_and_missing_log)
{
	img2_image_t *img = make_image(16, 16, false);
	uint8_t *buf = NULL;
	size_t len = 0;
	ASSERT_EQUAL(IMG2_OK, img2_encoder_dispatch(img, IMG2_FORMAT_AVIF, 50, &buf, &len));
	ASSERT_EQUAL(IMG2_MIME_AVIF, img2_detect_mime(buf, len));
	free(buf);
	img2_image_destroy(img);

	/* the "no encoder" lines name AVIF (a host libheif without aom takes this path) */
	img2_encoder_log_missing(IMG2_FORMAT_AVIF);
}
