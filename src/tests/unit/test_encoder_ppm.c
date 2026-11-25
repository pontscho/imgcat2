/**
 * @file test_encoder_ppm.c
 * @brief Unit tests for the native PPM / PGM encoder (encoders/encoder_ppm.c)
 *
 * Exact P6 / P5 headers, byte-exact round trips through img2_decode() (decoders/decoder_pnm.c), the P5 choice
 * (every composited pixel gray; one colour pixel at the very end gives P6), alpha compositing onto the default
 * black and a custom background (a = 0, 128, 255 and every alpha against a rounding reference), every "has no
 * effect" WARN exactly once, lossless silent, background range check, img2_can_encode(), the registry row,
 * img2_encoder_log_missing() and NULL arguments of both entry points.
 */

#include <img2/img2.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/encoders/encoder.h"
#include "../ctest.h"

/* ---- log capture (as test_encode_options.c) ------------------------------------------------------------- */

#define MAX_WARNS 16
static char s_warn[MAX_WARNS][256];
static int s_warn_count;
static char s_error[MAX_WARNS][256];
static int s_error_count;

static void capture_log(img2_log_level_t level, const char *msg, void *user)
{
	(void)user;
	if (level == IMG2_LOG_WARN) {
		if (s_warn_count < MAX_WARNS) {
			snprintf(s_warn[s_warn_count], sizeof(s_warn[0]), "%s", msg);
		}
		s_warn_count++;
	} else if (level == IMG2_LOG_ERROR) {
		if (s_error_count < MAX_WARNS) {
			snprintf(s_error[s_error_count], sizeof(s_error[0]), "%s", msg);
		}
		s_error_count++;
	}
}

static void capture_begin(void)
{
	s_warn_count = 0;
	s_error_count = 0;
	img2_set_log_callback(capture_log, NULL);
	img2_set_log_level(IMG2_LOG_WARN);
}

static void capture_end(void)
{
	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(IMG2_LOG_ERROR);
}

/** exactly one captured WARN equals @p text */
static void assert_one_warn(const char *text)
{
	int n = 0;
	for (int i = 0; i < s_warn_count && i < MAX_WARNS; i++) {
		if (strcmp(s_warn[i], text) == 0) {
			n++;
		}
	}
	if (n != 1) {
		fprintf(stderr, "expected one WARN '%s', got %d of %d:\n", text, n, s_warn_count);
		for (int i = 0; i < s_warn_count && i < MAX_WARNS; i++) {
			fprintf(stderr, "  '%s'\n", s_warn[i]);
		}
	}
	ASSERT_EQUAL(1, n);
}

/* ---- helpers -------------------------------------------------------------------------------------------- */

/* opaque colour gradient (R != G for most pixels) */
static img2_image_t *make_colour(uint32_t w, uint32_t h)
{
	img2_image_t *img = img2_image_alloc(w, h);
	ASSERT_NOT_NULL(img);
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint8_t *p = img2_image_get_pixel(img, x, y);
			p[0] = (uint8_t)(x * 255 / w);
			p[1] = (uint8_t)(y * 255 / h);
			p[2] = (uint8_t)(x * 7 + y * 13);
			p[3] = 255;
		}
	}
	return img;
}

/* opaque gray ramp (R == G == B) */
static img2_image_t *make_gray(uint32_t w, uint32_t h)
{
	img2_image_t *img = img2_image_alloc(w, h);
	ASSERT_NOT_NULL(img);
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint8_t *p = img2_image_get_pixel(img, x, y);
			const uint8_t v = (uint8_t)((x + y * w) * 37);
			p[0] = v;
			p[1] = v;
			p[2] = v;
			p[3] = 255;
		}
	}
	return img;
}

static img2_status_t encode(img2_image_t *img, const img2_encode_opts_t *eo, uint8_t **buf, size_t *len)
{
	img2_frames_t in = { &img, 1, 0 };
	*buf = NULL;
	*len = 0;
	return img2_encode(&in, IMG2_FORMAT_PPM, eo, buf, len);
}

/** encode (must succeed) and check the header text; returns the buffer (img2_free) */
static uint8_t *encode_expect_header(img2_image_t *img, const img2_encode_opts_t *eo, const char *header, size_t samples_per_pixel, size_t *len)
{
	uint8_t *buf = NULL;
	ASSERT_EQUAL(IMG2_OK, encode(img, eo, &buf, len));
	ASSERT_NOT_NULL(buf);
	const size_t hl = strlen(header);
	ASSERT_EQUAL_U(hl + (size_t)img->width * img->height * samples_per_pixel, *len);
	ASSERT_DATA((const unsigned char *)header, hl, buf, hl);
	return buf;
}

/** decode @p buf (must be PNM) and compare every RGBA byte with @p img */
static void assert_round_trip(const img2_image_t *img, const uint8_t *buf, size_t len)
{
	ASSERT_EQUAL(IMG2_MIME_PNM, img2_detect_mime(buf, len));
	img2_frames_t out;
	ASSERT_EQUAL(IMG2_OK, img2_decode(buf, len, NULL, &out));
	ASSERT_EQUAL(1, (int)out.count);
	const img2_image_t *d = out.frames[0];
	ASSERT_EQUAL(img->width, d->width);
	ASSERT_EQUAL(img->height, d->height);
	for (uint32_t y = 0; y < img->height; y++) {
		ASSERT_EQUAL(0, memcmp(img->pixels + (size_t)y * img->stride, d->pixels + (size_t)y * d->stride, (size_t)img->width * 4));
	}
	img2_frames_free(&out);
}

/** a 1x1 image of @p px encoded with @p bg; the composited RGB in @p rgb (P5 or P6, expanded to RGB) */
static void composite_one(const uint8_t px[4], uint32_t bg, uint8_t rgb[3])
{
	img2_image_t *img = img2_image_alloc(1, 1);
	ASSERT_NOT_NULL(img);
	memcpy(img->pixels, px, 4);
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.background = bg;
	uint8_t *buf = NULL;
	size_t len = 0;
	ASSERT_EQUAL(IMG2_OK, encode(img, &eo, &buf, &len));
	ASSERT_TRUE(len > 11);
	if (buf[1] == '5') {
		ASSERT_EQUAL_U(strlen("P5\n1 1\n255\n") + 1, len);
		rgb[0] = rgb[1] = rgb[2] = buf[len - 1];
	} else {
		ASSERT_EQUAL('6', buf[1]);
		ASSERT_EQUAL_U(strlen("P6\n1 1\n255\n") + 3, len);
		memcpy(rgb, buf + len - 3, 3);
	}
	img2_free(buf);
	img2_image_destroy(img);
}

/* ---- format ---------------------------------------------------------------------------------------------- */

CTEST(encoder_ppm, p6_header_and_round_trip)
{
	img2_image_t *img = make_colour(37, 23);
	size_t len = 0;
	uint8_t *buf = encode_expect_header(img, NULL, "P6\n37 23\n255\n", 3, &len);
	assert_round_trip(img, buf, len);

	/* NULL, IMG2_ENCODE_OPTS_INIT, the 4-argument entry point and the dispatcher give the same bytes */
	img2_encode_opts_t init = IMG2_ENCODE_OPTS_INIT;
	size_t len2 = 0;
	uint8_t *buf2 = encode_expect_header(img, &init, "P6\n37 23\n255\n", 3, &len2);
	ASSERT_DATA(buf, len, buf2, len2);
	img2_free(buf2);
	uint8_t *direct = NULL;
	size_t direct_len = 0;
	ASSERT_EQUAL(IMG2_OK, img2_encode_ppm(img, 0, &direct, &direct_len));
	ASSERT_DATA(buf, len, direct, direct_len);
	free(direct);
	ASSERT_EQUAL(IMG2_OK, img2_encoder_dispatch(img, IMG2_FORMAT_PPM, 0, &direct, &direct_len));
	ASSERT_DATA(buf, len, direct, direct_len);
	free(direct);
	img2_free(buf);
	img2_image_destroy(img);
}

CTEST(encoder_ppm, p5_gray_round_trip)
{
	img2_image_t *img = make_gray(9, 4);
	size_t len = 0;
	uint8_t *buf = encode_expect_header(img, NULL, "P5\n9 4\n255\n", 1, &len);
	const size_t hl = strlen("P5\n9 4\n255\n");
	for (size_t i = 0; i < (size_t)9 * 4; i++) {
		ASSERT_EQUAL(img->pixels[i * 4], buf[hl + i]);
	}
	assert_round_trip(img, buf, len);
	img2_free(buf);
	img2_image_destroy(img);
}

CTEST(encoder_ppm, one_colour_pixel_at_the_end_is_p6)
{
	img2_image_t *img = make_gray(6, 5);
	uint8_t *last = img2_image_get_pixel(img, 5, 4);
	last[2] = (uint8_t)(last[0] + 1);
	size_t len = 0;
	uint8_t *buf = encode_expect_header(img, NULL, "P6\n6 5\n255\n", 3, &len);
	assert_round_trip(img, buf, len);
	img2_free(buf);
	img2_image_destroy(img);
}

/* ---- alpha compositing ----------------------------------------------------------------------------------- */

CTEST(encoder_ppm, composite_default_black)
{
	uint8_t rgb[3];
	static const uint8_t opaque[4] = { 200, 100, 50, 255 };
	composite_one(opaque, 0u, rgb);
	ASSERT_DATA(opaque, 3, rgb, 3);

	static const uint8_t half[4] = { 200, 100, 50, 128 };
	static const uint8_t half_expect[3] = { 100, 50, 25 }; /* round(c * 128 / 255) */
	composite_one(half, 0u, rgb);
	ASSERT_DATA(half_expect, 3, rgb, 3);

	/* fully transparent colour on black: black, so P5 */
	static const uint8_t clear[4] = { 200, 100, 50, 0 };
	static const uint8_t black[3] = { 0, 0, 0 };
	composite_one(clear, 0u, rgb);
	ASSERT_DATA(black, 3, rgb, 3);

	/* a transparent image on black is a P5 of zeros */
	img2_image_t *img = make_colour(4, 3);
	for (size_t i = 0; i < (size_t)4 * 3; i++) {
		img->pixels[i * 4 + 3] = 0;
	}
	size_t len = 0;
	uint8_t *buf = encode_expect_header(img, NULL, "P5\n4 3\n255\n", 1, &len);
	const size_t hl = strlen("P5\n4 3\n255\n");
	for (size_t i = hl; i < len; i++) {
		ASSERT_EQUAL(0, buf[i]);
	}
	img2_free(buf);
	img2_image_destroy(img);
}

CTEST(encoder_ppm, composite_custom_background)
{
	const uint32_t bg = 0x204080u;
	uint8_t rgb[3];
	static const uint8_t opaque[4] = { 200, 100, 50, 255 };
	composite_one(opaque, bg, rgb);
	ASSERT_DATA(opaque, 3, rgb, 3);

	static const uint8_t half[4] = { 200, 100, 50, 128 };
	static const uint8_t half_expect[3] = { 116, 82, 89 }; /* round((c * 128 + bg * 127) / 255) */
	composite_one(half, bg, rgb);
	ASSERT_DATA(half_expect, 3, rgb, 3);

	static const uint8_t clear[4] = { 200, 100, 50, 0 };
	static const uint8_t bg_rgb[3] = { 0x20, 0x40, 0x80 };
	composite_one(clear, bg, rgb);
	ASSERT_DATA(bg_rgb, 3, rgb, 3);

	/* every alpha against the rounding reference, on white (a gray result: P5 when c is gray too) */
	for (int a = 0; a <= 255; a++) {
		const uint8_t px[4] = { 10, 250, 128, (uint8_t)a };
		composite_one(px, 0xFFFFFFu, rgb);
		for (int c = 0; c < 3; c++) {
			const long ref = lround(((double)px[c] * a + 255.0 * (255 - a)) / 255.0);
			ASSERT_EQUAL((int)ref, (int)rgb[c]);
		}
	}

	/* a transparent image on a gray background: P5 of the background */
	img2_image_t *img = make_colour(3, 2);
	for (size_t i = 0; i < (size_t)3 * 2; i++) {
		img->pixels[i * 4 + 3] = 0;
	}
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.background = 0x7F7F7Fu;
	capture_begin();
	size_t len = 0;
	uint8_t *buf = encode_expect_header(img, &eo, "P5\n3 2\n255\n", 1, &len);
	capture_end();
	ASSERT_EQUAL(0, s_warn_count); /* background is honoured by PPM: no WARN */
	const size_t hl = strlen("P5\n3 2\n255\n");
	for (size_t i = hl; i < len; i++) {
		ASSERT_EQUAL(0x7F, buf[i]);
	}
	img2_free(buf);
	img2_image_destroy(img);
}

/* ---- options --------------------------------------------------------------------------------------------- */

CTEST(encoder_ppm, warnings)
{
	img2_image_t *img = make_colour(8, 8);
	uint8_t *ref = NULL;
	size_t ref_len = 0;
	ASSERT_EQUAL(IMG2_OK, encode(img, NULL, &ref, &ref_len));

	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.quality = 50;
	eo.effort = 9;
	eo.chroma = IMG2_CHROMA_444;
	eo.alpha_quality = 50;
	eo.progressive = true;
	eo.threads = 2;
	eo.metadata = IMG2_META_EXIF | IMG2_META_XMP;
	uint8_t *buf = NULL;
	size_t len = 0;
	capture_begin();
	ASSERT_EQUAL(IMG2_OK, encode(img, &eo, &buf, &len));
	capture_end();
	ASSERT_EQUAL(7, s_warn_count);
	assert_one_warn("encode: quality has no effect for PPM (uncompressed)");
	assert_one_warn("encode: effort has no effect for PPM (uncompressed)");
	assert_one_warn("encode: chroma 444 has no effect for PPM (always RGB or gray)");
	assert_one_warn("encode: alpha_quality has no effect for PPM (no alpha channel)");
	assert_one_warn("encode: progressive has no effect for PPM (no progressive mode)");
	assert_one_warn("encode: threads has no effect for PPM (single-threaded encoder)");
	assert_one_warn("encode: metadata has no effect for PPM (no metadata container)");
	ASSERT_DATA(ref, ref_len, buf, len); /* ignored: the default bytes */
	img2_free(buf);

	/* one option at a time: exactly its WARN */
	img2_encode_opts_t c420 = IMG2_ENCODE_OPTS_INIT;
	c420.chroma = IMG2_CHROMA_420;
	capture_begin();
	ASSERT_EQUAL(IMG2_OK, encode(img, &c420, &buf, &len));
	capture_end();
	ASSERT_EQUAL(1, s_warn_count);
	assert_one_warn("encode: chroma 420 has no effect for PPM (always RGB or gray)");
	img2_free(buf);

	/* lossless: a silent no-op (always lossless) */
	img2_encode_opts_t ll = IMG2_ENCODE_OPTS_INIT;
	ll.lossless = true;
	capture_begin();
	ASSERT_EQUAL(IMG2_OK, encode(img, &ll, &buf, &len));
	capture_end();
	ASSERT_EQUAL(0, s_warn_count);
	ASSERT_DATA(ref, ref_len, buf, len);
	img2_free(buf);
	img2_free(ref);
	img2_image_destroy(img);
}

CTEST(encoder_ppm, background_out_of_range)
{
	img2_image_t *img = make_colour(4, 4);
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.background = 0x1000000u;
	uint8_t *buf = (uint8_t *)1;
	size_t len = 1;
	ASSERT_EQUAL(IMG2_E_ARG, encode(img, &eo, &buf, &len));
	ASSERT_NULL(buf);
	ASSERT_EQUAL_U(0u, len);
	eo.background = 0xFFFFFFu; /* the edge is valid */
	ASSERT_EQUAL(IMG2_OK, encode(img, &eo, &buf, &len));
	img2_free(buf);
	img2_image_destroy(img);
}

/* ---- registry and arguments ------------------------------------------------------------------------------ */

CTEST(encoder_ppm, can_encode_and_table_row)
{
	ASSERT_TRUE(img2_can_encode(IMG2_FORMAT_PPM));
	bool found = false;
	for (size_t i = 0; i < img2_encoder_count(); i++) {
		const img2_encoder_info_t *info = img2_encoder_info(i);
		if (info->format == IMG2_FORMAT_PPM) {
			ASSERT_FALSE(found);
			found = true;
			ASSERT_STR("PPM (native)", info->name);
			ASSERT_STR(".ppm", info->extension);
		}
	}
	ASSERT_TRUE(found);
	const img2_encoder_entry_t *entry = img2_encoder_find(IMG2_FORMAT_PPM);
	ASSERT_NOT_NULL(entry);
	ASSERT_EQUAL(0, entry->default_quality);
	ASSERT_TRUE(entry->available == NULL);

	/* the "no encoder" lines name PPM */
	capture_begin();
	img2_encoder_log_missing(IMG2_FORMAT_PPM);
	capture_end();
	ASSERT_EQUAL(2, s_error_count);
	ASSERT_STR("Error: No encoder found for format: PPM", s_error[0]);
}

CTEST(encoder_ppm, null_arguments)
{
	img2_image_t *img = make_colour(2, 2);
	uint8_t *buf = (uint8_t *)1;
	size_t len = 1;
	capture_begin();
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode_ppm(NULL, 0, &buf, &len));
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode_ppm(img, 0, NULL, &len));
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode_ppm(img, 0, &buf, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode_ppm_opts(NULL, 0, NULL, &buf, &len));
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode_ppm_opts(img, 0, NULL, NULL, &len));
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode_ppm_opts(img, 0, NULL, &buf, NULL));
	capture_end();
	ASSERT_EQUAL(6, s_error_count);
	ASSERT_STR("Error: Invalid parameters to encode_ppm", s_error[0]);

	/* outputs are reset on success */
	ASSERT_EQUAL(IMG2_OK, img2_encode_ppm_opts(img, 0, NULL, &buf, &len));
	ASSERT_NOT_NULL(buf);
	ASSERT_EQUAL_U(strlen("P6\n2 2\n255\n") + 12, len);
	free(buf);
	img2_image_destroy(img);
}
