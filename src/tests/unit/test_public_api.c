/**
 * @file test_public_api.c
 * @brief Black-box test of the public libimg2 API ([L4]: links img2 only, includes only <img2/img2.h>)
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

static int s_log_count;

static void count_log(img2_log_level_t level, const char *msg, void *user)
{
	(void)level;
	(void)msg;
	(void)user;
	s_log_count++;
}

static void quiet_log(img2_log_level_t level, const char *msg, void *user)
{
	(void)level;
	(void)msg;
	(void)user;
}

/* Caller-built image over a dummy buffer */
static img2_image_t make_image(uint8_t *pixels, uint32_t w, uint32_t h, uint32_t stride)
{
	img2_image_t img;
	memset(&img, 0, sizeof(img));
	img.width = w;
	img.height = h;
	img.stride = stride;
	img.pixels = pixels;
	return img;
}

CTEST(public_api, version_and_init_refcount)
{
	ASSERT_STR(IMG2_VERSION_STRING, img2_version_string());
	ASSERT_EQUAL(IMG2_OK, img2_init());
	ASSERT_EQUAL(IMG2_OK, img2_init());
	img2_shutdown();
	ASSERT_TRUE(img2_decoder_count() > 0);
	img2_shutdown();
	img2_shutdown(); /* unbalanced: ignored */
	ASSERT_EQUAL(IMG2_OK, img2_init());
	img2_shutdown();
}

CTEST(public_api, strerror_every_code)
{
	for (int st = IMG2_E_NOT_FOUND; st <= IMG2_OK; st++) {
		ASSERT_NOT_NULL(img2_strerror((img2_status_t)st));
	}
	ASSERT_NOT_NULL(img2_strerror((img2_status_t)-1000));
}

CTEST(public_api, decode_png_file)
{
	size_t len = 0;
	uint8_t *buf = load_file("data/png/goldhill.png", &len);
	ASSERT_NOT_NULL(buf);

	img2_frames_t fr;
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	ASSERT_EQUAL(IMG2_OK, img2_decode(buf, len, &opts, &fr));
	ASSERT_EQUAL(1, fr.count);
	ASSERT_NOT_NULL(fr.frames);
	ASSERT_NOT_NULL(fr.frames[0]);
	ASSERT_TRUE(fr.frames[0]->width > 0 && fr.frames[0]->height > 0);
	ASSERT_EQUAL((intmax_t)fr.frames[0]->width * 4, fr.frames[0]->stride);

	/* img2_scale DEFAULT gives the requested dimensions */
	img2_image_t *scaled = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_scale(fr.frames[0], 64, 32, IMG2_FILTER_DEFAULT, &scaled));
	ASSERT_NOT_NULL(scaled);
	ASSERT_EQUAL(64, scaled->width);
	ASSERT_EQUAL(32, scaled->height);
	ASSERT_EQUAL(64 * 4, scaled->stride);
	img2_image_destroy(scaled);

	/* img2_scale_fit keeps the aspect ratio inside the box */
	ASSERT_EQUAL(IMG2_OK, img2_scale_fit(fr.frames[0], 100, 100, IMG2_FILTER_DEFAULT, &scaled));
	ASSERT_NOT_NULL(scaled);
	ASSERT_TRUE(scaled->width <= 100 && scaled->height <= 100);
	ASSERT_TRUE(scaled->width == 100 || scaled->height == 100);
	img2_image_destroy(scaled);

	ASSERT_TRUE(img2_is_animated(buf, len) == false);
	img2_frames_free(&fr);
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);
	free(buf);
}

CTEST(public_api, png_round_trip)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(8, 6, &img));
	for (uint32_t y = 0; y < 6; y++) {
		for (uint32_t x = 0; x < 8; x++) {
			ASSERT_TRUE(img2_image_set_pixel(img, x, y, (uint8_t)(x * 30), (uint8_t)(y * 40), (uint8_t)(x ^ y), 255));
		}
	}
	ASSERT_NULL(img2_image_get_pixel(img, 8, 0));
	ASSERT_FALSE(img2_image_set_pixel(img, 0, 6, 0, 0, 0, 0));

	img2_frames_t in = { &img, 1, 0 };
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	uint8_t *png = NULL;
	size_t png_len = 0;
	ASSERT_TRUE(img2_can_encode(IMG2_FORMAT_PNG));
	ASSERT_EQUAL(IMG2_OK, img2_encode(&in, IMG2_FORMAT_PNG, &eo, &png, &png_len));
	ASSERT_NOT_NULL(png);
	ASSERT_TRUE(png_len > 8);
	ASSERT_EQUAL(IMG2_MIME_PNG, img2_detect_mime(png, png_len));

	img2_frames_t out;
	ASSERT_EQUAL(IMG2_OK, img2_decode(png, png_len, NULL, &out));
	ASSERT_EQUAL(1, out.count);
	ASSERT_EQUAL(8, out.frames[0]->width);
	ASSERT_EQUAL(6, out.frames[0]->height);
	ASSERT_DATA(img->pixels, (size_t)8 * 6 * 4, out.frames[0]->pixels, (size_t)out.frames[0]->stride * out.frames[0]->height);

	img2_frames_free(&out);
	img2_free(png);
	img2_image_destroy(img);
}

CTEST(public_api, encode_format_checks)
{
	img2_image_t *img = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(4, 4, &img));
	img2_frames_t in = { &img, 1, 0 };
	uint8_t *buf = (uint8_t *)1;
	size_t len = 1;

	ASSERT_EQUAL(IMG2_E_ARG, img2_encode(&in, IMG2_FORMAT_NONE, NULL, &buf, &len));
	ASSERT_NULL(buf);
	ASSERT_EQUAL(0, len);

	for (int f = IMG2_FORMAT_JPEG; f <= IMG2_FORMAT_JXL; f++) {
		if (!img2_can_encode((img2_format_t)f)) {
			ASSERT_EQUAL(IMG2_E_UNSUPPORTED, img2_encode(&in, (img2_format_t)f, NULL, &buf, &len));
			ASSERT_NULL(buf);
		}
	}

	/* encoder table rows are consistent with img2_can_encode */
	for (size_t i = 0; i < img2_encoder_count(); i++) {
		const img2_encoder_info_t *info = img2_encoder_info(i);
		ASSERT_NOT_NULL(info);
		ASSERT_TRUE(img2_can_encode(info->format));
	}
	ASSERT_NULL(img2_encoder_info(img2_encoder_count()));
	ASSERT_NULL(img2_decoder_info(img2_decoder_count()));
	img2_image_destroy(img);
}

CTEST(public_api, log_callback_garbage)
{
	/* [Q6] 0xA5 pattern; byte 2 = 0x00 and byte 16 = 0x07 defeat the TGA header heuristic */
	uint8_t garbage[64];
	memset(garbage, 0xA5, sizeof(garbage));
	garbage[2] = 0x00;
	garbage[16] = 0x07;
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, img2_detect_mime(garbage, sizeof(garbage)));

	s_log_count = 0;
	img2_set_log_callback(count_log, NULL);
	img2_frames_t fr;
	img2_status_t st = img2_decode(garbage, sizeof(garbage), NULL, &fr);
	img2_set_log_callback(NULL, NULL);

	ASSERT_EQUAL(IMG2_E_FORMAT, st);
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);
	ASSERT_TRUE(s_log_count >= 1);
	ASSERT_EQUAL(IMG2_LOG_ERROR, img2_get_log_level());
}

CTEST(public_api, decode_boundaries)
{
	uint8_t buf64[64];
	memset(buf64, 0, sizeof(buf64));
	img2_frames_t fr;

	img2_frames_free(NULL); /* NULL-safe */

	memset(&fr, 0xFF, sizeof(fr));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode(NULL, 10, NULL, &fr));
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);
	ASSERT_EQUAL(0, fr.loop_count);

	memset(&fr, 0xFF, sizeof(fr));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode(buf64, 0, NULL, &fr));
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);

	ASSERT_EQUAL(IMG2_E_ARG, img2_decode(buf64, sizeof(buf64), NULL, NULL));

	/* the length check runs before any read */
	memset(&fr, 0xFF, sizeof(fr));
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_decode(buf64, (size_t)IMG2_MAX_FILE_SIZE + 1, NULL, &fr));
	ASSERT_NULL(fr.frames);
	ASSERT_FALSE(img2_is_animated(buf64, (size_t)IMG2_MAX_FILE_SIZE + 1));
	ASSERT_FALSE(img2_is_animated(NULL, 10));
}

CTEST(public_api, image_create)
{
	img2_image_t *img = (img2_image_t *)1;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_create(0, 1, &img));
	ASSERT_NULL(img);
	img = (img2_image_t *)1;
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_image_create(70000, 1, &img));
	ASSERT_NULL(img);
	ASSERT_EQUAL(IMG2_OK, img2_image_create(4, 4, &img));
	ASSERT_NOT_NULL(img);
	ASSERT_EQUAL(16, img->stride);
	ASSERT_NOT_NULL(img2_image_get_pixel(img, 3, 3));
	ASSERT_EQUAL(0, img2_image_get_pixel(img, 3, 3)[3]);
	img2_image_destroy(img);
	img2_image_destroy(NULL);
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_create(1, 1, NULL));
}

CTEST(public_api, fit_size)
{
	uint32_t w = 0, h = 0;
	ASSERT_EQUAL(IMG2_OK, img2_fit_size(200, 100, 100, 100, &w, &h)); /* wide */
	ASSERT_EQUAL(100, w);
	ASSERT_EQUAL(50, h);
	ASSERT_EQUAL(IMG2_OK, img2_fit_size(100, 200, 100, 100, &w, &h)); /* tall */
	ASSERT_EQUAL(50, w);
	ASSERT_EQUAL(100, h);
	ASSERT_EQUAL(IMG2_OK, img2_fit_size(300, 300, 120, 80, &w, &h)); /* square into landscape */
	ASSERT_EQUAL(80, w);
	ASSERT_EQUAL(80, h);

	ASSERT_EQUAL(IMG2_E_ARG, img2_fit_size(0, 1, 1, 1, &w, &h));
	ASSERT_EQUAL(IMG2_E_ARG, img2_fit_size(1, 0, 1, 1, &w, &h));
	ASSERT_EQUAL(IMG2_E_ARG, img2_fit_size(1, 1, 0, 1, &w, &h));
	ASSERT_EQUAL(IMG2_E_ARG, img2_fit_size(1, 1, 1, 0, &w, &h));
	ASSERT_EQUAL(IMG2_E_ARG, img2_fit_size(1, 1, 1, 1, NULL, &h));

	/* outputs never exceed the box (fixed-seed LCG; degenerate results may be IMG2_E_ARG) */
	img2_set_log_callback(quiet_log, NULL);
	uint32_t seed = 12345u;
	for (int i = 0; i < 1000; i++) {
		uint32_t v[4];
		for (int k = 0; k < 4; k++) {
			seed = seed * 1664525u + 1013904223u;
			v[k] = 1 + (seed >> 8) % 5000;
		}
		img2_status_t st = img2_fit_size(v[0], v[1], v[2], v[3], &w, &h);
		if (st == IMG2_OK) {
			ASSERT_TRUE(w >= 1 && w <= v[2]);
			ASSERT_TRUE(h >= 1 && h <= v[3]);
		} else {
			ASSERT_EQUAL(IMG2_E_ARG, st);
		}
	}
	img2_set_log_callback(NULL, NULL);
}

CTEST(public_api, caller_built_images_rejected)
{
	static uint8_t px[64 * 4];
	img2_image_t bad[6];
	bad[0] = make_image(NULL, 4, 4, 16);                 /* pixels == NULL */
	bad[1] = make_image(px, 0, 4, 0);                    /* width == 0 */
	bad[2] = make_image(px, 70000, 1, 280000);           /* width over the limit */
	bad[3] = make_image(px, 4, 4, 15);                   /* stride < width * 4 */
	bad[4] = make_image(px, 4, 4, 20);                   /* padded rows unsupported in 1.0 */
	bad[5] = make_image(px, 1, 65536, UINT32_MAX);       /* stride * height overflow / not packed */

	img2_set_log_callback(quiet_log, NULL);
	for (int i = 0; i < 6; i++) {
		img2_image_t *out = (img2_image_t *)1;
		ASSERT_EQUAL(IMG2_E_ARG, img2_scale(&bad[i], 2, 2, IMG2_FILTER_DEFAULT, &out));
		ASSERT_NULL(out);
		out = (img2_image_t *)1;
		ASSERT_EQUAL(IMG2_E_ARG, img2_scale_fit(&bad[i], 2, 2, IMG2_FILTER_DEFAULT, &out));
		ASSERT_NULL(out);

		img2_image_t *p = &bad[i];
		img2_frames_t in = { &p, 1, 0 };
		uint8_t *buf = NULL;
		size_t len = 0;
		ASSERT_EQUAL(IMG2_E_ARG, img2_encode(&in, IMG2_FORMAT_PNG, NULL, &buf, &len));
		ASSERT_NULL(buf);
		ASSERT_EQUAL(IMG2_E_ARG, img2_apply_orientation(&p));
	}
	ASSERT_EQUAL(IMG2_E_ARG, img2_apply_orientation(NULL));

	/* bad frame sets */
	uint8_t *buf = NULL;
	size_t len = 0;
	img2_frames_t no_array = { NULL, 1, 0 };
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode(&no_array, IMG2_FORMAT_PNG, NULL, &buf, &len));
	img2_image_t *good = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_create(4, 4, &good));
	img2_frames_t negative = { &good, -1, 0 };
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode(&negative, IMG2_FORMAT_PNG, NULL, &buf, &len));

	/* valid source, bad targets */
	img2_image_t *out = NULL;
	ASSERT_EQUAL(IMG2_E_ARG, img2_scale(good, 0, 5, IMG2_FILTER_DEFAULT, &out));
	ASSERT_NULL(out);
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_scale(good, 70000, 1, IMG2_FILTER_DEFAULT, &out));
	ASSERT_NULL(out);

	/* no orientation -> no-op */
	ASSERT_EQUAL(IMG2_OK, img2_apply_orientation(&good));
	ASSERT_EQUAL(4, good->width);
	img2_image_destroy(good);
	img2_set_log_callback(NULL, NULL);
}

CTEST(public_api, yuv_frame_size_overflow)
{
	img2_yuv_params_t p;
	memset(&p, 0, sizeof(p));
	p.format = IMG2_YUV_FMT_I420;
	p.width = 65536;
	p.height = 65536;
	p.y_stride = SIZE_MAX / 2;
	size_t size = 0;
	ASSERT_FALSE(img2_yuv_frame_size(&p, &size));

	char err[128];
	img2_yuv_params_t q;
	ASSERT_EQUAL(IMG2_OK, img2_yuv_parse_spec("nv12:16:8", &q, err, sizeof(err)));
	ASSERT_EQUAL(IMG2_YUV_FMT_NV12, q.format);
	ASSERT_TRUE(img2_yuv_frame_size(&q, &size));
	ASSERT_EQUAL(16 * 8 * 3 / 2, size);
	ASSERT_EQUAL(IMG2_YUV_FMT_I420, img2_yuv_format_from_name("yuv420p"));
}

static int count_embedded(const img2_font_info_t *info, void *user)
{
	if (info->embedded) {
		(*(int *)user)++;
	}
	return 0;
}

CTEST(public_api, fonts)
{
	int embedded = 0;
	ASSERT_EQUAL(IMG2_OK, img2_font_foreach(count_embedded, &embedded));
	ASSERT_TRUE(embedded >= 4);
	ASSERT_EQUAL(IMG2_E_ARG, img2_font_foreach(NULL, NULL));

	img2_font_t *f = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_font_load_fallback(&f));
	ASSERT_NOT_NULL(f);
	ASSERT_STR("DejaVu Sans Mono", img2_font_get_family(f));
	size_t size = 0;
	ASSERT_NOT_NULL(img2_font_get_data(f, &size));
	ASSERT_TRUE(size > 0);

	img2_font_t *g = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_font_load("no-such-family-xyz", IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &g));
	ASSERT_NOT_NULL(g);
	ASSERT_STR("DejaVu Sans Mono", img2_font_get_family(g));

	g = (img2_font_t *)1;
	ASSERT_EQUAL(IMG2_E_ARG, img2_font_load(NULL, IMG2_FONT_WEIGHT_NORMAL, IMG2_FONT_STYLE_NORMAL, &g));
	ASSERT_NULL(g);
	ASSERT_EQUAL(IMG2_E_ARG, img2_font_load_fallback(NULL));
}
