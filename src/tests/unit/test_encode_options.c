/**
 * @file test_encode_options.c
 * @brief img2_encode_opts_t beyond quality (docs/encoder-api.md phase 2)
 *
 * IMG2_ENCODE_OPTS_INIT / zero-init defaults, IMG2_E_ARG for every out-of-range field, and per format: lossless
 * round trips by memcmp (PNG, WebP, JXL, AVIF), chroma read back from the file (JPEG comp_info sampling factors,
 * AVIF libheif chroma), progressive JPEG (jpeg_has_multiple_scans) and interlaced PNG (IHDR), effort / threads
 * as "valid and decodable" (plus the mappings that must equal a default: WebP effort 7 = method 4, JXL effort 7,
 * AVIF effort 4 = speed 6, PNG effort e = zlib level e-1), the lossy WebPConfig path against libwebp's simple API,
 * lossless WebP keeping the RGB under alpha 0 (`exact`), lossy JXL progressive (more decoder progression steps),
 * and every "has no effect" WARN exactly once.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* clang-format off */
#include <jpeglib.h>
#include <libheif/heif.h>
#ifdef IMG2_HAVE_WEBP
#include <webp/encode.h>
#endif
#ifdef IMG2_HAVE_JXL
#include <jxl/decode.h>
#endif
/* clang-format on */

#include <img2/img2.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/encoders/encoder.h"
#include "../ctest.h"

/* ---- log capture ---------------------------------------------------------------------------------------- */

#define MAX_WARNS 16
static char s_warn[MAX_WARNS][256];
static int s_warn_count;

static void capture_log(img2_log_level_t level, const char *msg, void *user)
{
	(void)user;
	if (level == IMG2_LOG_WARN) {
		if (s_warn_count < MAX_WARNS) {
			snprintf(s_warn[s_warn_count], sizeof(s_warn[0]), "%s", msg);
		}
		s_warn_count++;
	}
}

static void capture_begin(void)
{
	s_warn_count = 0;
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

/* gradient + checker; alpha 1..255 (alpha 0: make_image_transparent()) */
static img2_image_t *make_image(uint32_t w, uint32_t h, bool with_alpha)
{
	img2_image_t *img = img2_image_alloc(w, h);
	ASSERT_NOT_NULL(img);
	for (uint32_t y = 0; y < h; y++) {
		for (uint32_t x = 0; x < w; x++) {
			uint8_t *p = img2_image_get_pixel(img, x, y);
			p[0] = (uint8_t)(x * 255 / w);
			p[1] = (uint8_t)(y * 255 / h);
			p[2] = ((x / 8 + y / 8) % 2) ? 200 : (uint8_t)(x * 7 + y * 13);
			p[3] = with_alpha ? (uint8_t)(1 + (x * 254 / w)) : 255;
		}
	}
	return img;
}

/* make_image() with a fully transparent block (alpha 0) whose RGB is not black: "lossless" must keep it */
static img2_image_t *make_image_transparent(uint32_t w, uint32_t h)
{
	img2_image_t *img = make_image(w, h, true);
	for (uint32_t y = 0; y < h / 2; y++) {
		for (uint32_t x = 0; x < w / 2; x++) {
			img2_image_get_pixel(img, x, y)[3] = 0;
		}
	}
	return img;
}

static img2_status_t encode(img2_image_t *img, img2_format_t fmt, const img2_encode_opts_t *eo, uint8_t **buf, size_t *len)
{
	img2_frames_t in = { &img, 1, 0 };
	*buf = NULL;
	*len = 0;
	return img2_encode(&in, fmt, eo, buf, len);
}

/** encode (must succeed), decode (must succeed, same size); returns the decoded frames */
static void encode_decode(img2_image_t *img, img2_format_t fmt, const img2_encode_opts_t *eo, img2_frames_t *out)
{
	uint8_t *buf = NULL;
	size_t len = 0;
	ASSERT_EQUAL(IMG2_OK, encode(img, fmt, eo, &buf, &len));
	ASSERT_NOT_NULL(buf);
	ASSERT_TRUE(len > 0);
	ASSERT_EQUAL(IMG2_OK, img2_decode(buf, len, NULL, out));
	ASSERT_EQUAL(1, (int)out->count);
	ASSERT_EQUAL(img->width, out->frames[0]->width);
	ASSERT_EQUAL(img->height, out->frames[0]->height);
	img2_free(buf);
}

/** encode + decode must reproduce every RGBA byte */
static void assert_lossless(img2_image_t *img, img2_format_t fmt, const img2_encode_opts_t *eo)
{
	img2_frames_t out;
	encode_decode(img, fmt, eo, &out);
	const img2_image_t *d = out.frames[0];
	for (uint32_t y = 0; y < img->height; y++) {
		ASSERT_EQUAL(0, memcmp(img->pixels + (size_t)y * img->stride, d->pixels + (size_t)y * d->stride, (size_t)img->width * 4));
	}
	img2_frames_free(&out);
}

/** encode + decode must work (effort / threads: "valid and decodable") */
static void assert_decodable(img2_image_t *img, img2_format_t fmt, const img2_encode_opts_t *eo)
{
	img2_frames_t out;
	encode_decode(img, fmt, eo, &out);
	img2_frames_free(&out);
}

/** the two option sets must give the same bytes */
static void assert_same_bytes(img2_image_t *img, img2_format_t fmt, const img2_encode_opts_t *a, const img2_encode_opts_t *b)
{
	uint8_t *ba = NULL;
	uint8_t *bb = NULL;
	size_t la = 0;
	size_t lb = 0;
	ASSERT_EQUAL(IMG2_OK, encode(img, fmt, a, &ba, &la));
	ASSERT_EQUAL(IMG2_OK, encode(img, fmt, b, &bb, &lb));
	ASSERT_DATA(ba, la, bb, lb);
	img2_free(ba);
	img2_free(bb);
}

/* ---- defaults and validation ---------------------------------------------------------------------------- */

CTEST(encode_options, init_equals_defaults)
{
	img2_encode_opts_t init = IMG2_ENCODE_OPTS_INIT;
	ASSERT_EQUAL(-1, init.quality);
	ASSERT_FALSE(init.lossless);
	ASSERT_EQUAL(0, init.effort);
	ASSERT_EQUAL(IMG2_CHROMA_DEFAULT, init.chroma);
	ASSERT_EQUAL(0, init.alpha_quality);
	ASSERT_FALSE(init.progressive);
	ASSERT_EQUAL(0, init.threads);
	ASSERT_EQUAL_U(0u, init.metadata);
	ASSERT_EQUAL_U(0u, init.background);

	/* zero-init with quality -1 is the same struct; { .quality = q } leaves every new field at its default */
	img2_encode_opts_t zero;
	memset(&zero, 0, sizeof(zero));
	zero.quality = -1;
	ASSERT_EQUAL(0, memcmp(&init, &zero, sizeof(init)));
	img2_encode_opts_t q = { .quality = 50 };
	ASSERT_FALSE(q.lossless);
	ASSERT_EQUAL(0, q.effort + q.alpha_quality + q.threads + (int)q.chroma + (int)q.metadata + (int)q.background);
	ASSERT_FALSE(q.progressive);

	/* NULL, IMG2_ENCODE_OPTS_INIT and the registry default quality give the same bytes for every format */
	img2_image_t *img = make_image(32, 24, true);
	for (size_t i = 0; i < img2_encoder_count(); i++) {
		img2_format_t f = img2_encoder_info(i)->format;
		assert_same_bytes(img, f, NULL, &init);
		img2_encode_opts_t reg = IMG2_ENCODE_OPTS_INIT;
		reg.quality = f == IMG2_FORMAT_PNG ? 60 : img2_encoder_find(f)->default_quality;
		assert_same_bytes(img, f, &init, &reg);
	}
	img2_image_destroy(img);

	ASSERT_STR("420", img2_chroma_name(IMG2_CHROMA_420));
	ASSERT_STR("default", img2_chroma_name(IMG2_CHROMA_DEFAULT));
}

CTEST(encode_options, out_of_range_is_arg_error)
{
	img2_image_t *img = make_image(8, 8, false);
	uint8_t *buf = (uint8_t *)1;
	size_t len = 1;
	img2_encode_opts_t bad[13];
	for (int i = 0; i < 13; i++) {
		img2_encode_opts_t d = IMG2_ENCODE_OPTS_INIT;
		bad[i] = d;
	}
	bad[0].effort = -1;
	bad[1].effort = 11;
	bad[2].alpha_quality = -1;
	bad[3].alpha_quality = 101;
	bad[4].threads = -1;
	bad[5].threads = 65;
	bad[6].chroma = (img2_chroma_t)4;
	bad[7].chroma = (img2_chroma_t)-1;
	bad[8].metadata = 1u << 2;
	bad[9].metadata = 0x80000000u | IMG2_META_EXIF;
	bad[10].effort = 1000;
	bad[11].background = 0x1000000u;
	bad[12].background = 0xFFFFFFFFu;
	for (int i = 0; i < 13; i++) {
		ASSERT_EQUAL(IMG2_E_ARG, encode(img, IMG2_FORMAT_PNG, &bad[i], &buf, &len));
		ASSERT_NULL(buf);
		ASSERT_EQUAL(0, (int)len);
	}

	/* the edges of every range are valid */
	img2_encode_opts_t ok = IMG2_ENCODE_OPTS_INIT;
	ok.effort = 10;
	ok.alpha_quality = 100;
	ok.chroma = IMG2_CHROMA_444;
	ok.threads = 64;
	ok.background = 0xFFFFFFu;
	capture_begin();
	ASSERT_EQUAL(IMG2_OK, encode(img, IMG2_FORMAT_JPEG, &ok, &buf, &len));
	capture_end();
	img2_free(buf);
	img2_image_destroy(img);
}

/* Phase 3: metadata is written (test_encode_metadata); requested blocks the image does not have are silently absent */
CTEST(encode_options, metadata_absent_is_silent)
{
	img2_image_t *img = make_image(8, 8, false);
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.metadata = IMG2_META_EXIF | IMG2_META_XMP;
	uint8_t *a = NULL;
	size_t la = 0;
	capture_begin();
	ASSERT_EQUAL(IMG2_OK, encode(img, IMG2_FORMAT_PNG, &eo, &a, &la));
	capture_end();
	ASSERT_EQUAL(0, s_warn_count);

	/* the output is the default output */
	uint8_t *b = NULL;
	size_t lb = 0;
	ASSERT_EQUAL(IMG2_OK, encode(img, IMG2_FORMAT_PNG, NULL, &b, &lb));
	ASSERT_DATA(a, la, b, lb);
	img2_free(a);
	img2_free(b);
	img2_image_destroy(img);
}

/* background: only PPM composites alpha (test_encoder_ppm); every other format logs one WARN (from the dispatcher)
 * and gives the default bytes; background 0 is the default: no WARN */
CTEST(encode_options, background_warns_for_non_ppm)
{
	static const struct {
		img2_format_t format;
		const char *warn;
	} k_rows[] = {
		{ IMG2_FORMAT_JPEG, "encode: background has no effect for JPEG (alpha is stripped)" },
		{ IMG2_FORMAT_PNG,  "encode: background has no effect for PNG (alpha is kept)"     },
#ifdef IMG2_HAVE_WEBP
		{ IMG2_FORMAT_WEBP, "encode: background has no effect for WebP (alpha is kept)"    },
#endif
#ifdef IMG2_HAVE_JXL
		{ IMG2_FORMAT_JXL,  "encode: background has no effect for JXL (alpha is kept)"     },
#endif
		{ IMG2_FORMAT_HEIF, "encode: background has no effect for HEIF (alpha is kept)"    },
		{ IMG2_FORMAT_AVIF, "encode: background has no effect for AVIF (alpha is kept)"    },
	};
	img2_image_t *img = make_image(16, 16, true);
	for (size_t i = 0; i < sizeof(k_rows) / sizeof(k_rows[0]); i++) {
		img2_encode_opts_t bg = IMG2_ENCODE_OPTS_INIT;
		bg.background = 0xFFFFFFu;
		capture_begin();
		assert_same_bytes(img, k_rows[i].format, NULL, &bg);
		capture_end();
		ASSERT_EQUAL(1, s_warn_count);
		assert_one_warn(k_rows[i].warn);

		img2_encode_opts_t zero = IMG2_ENCODE_OPTS_INIT;
		zero.background = 0u;
		capture_begin();
		assert_same_bytes(img, k_rows[i].format, NULL, &zero);
		capture_end();
		ASSERT_EQUAL(0, s_warn_count);
	}
	img2_image_destroy(img);
}

/* ---- JPEG ----------------------------------------------------------------------------------------------- */

typedef struct {
	int h0, v0, h1, v1;
	bool progressive;
} jpeg_info_t;

static jpeg_info_t jpeg_read_info(const uint8_t *buf, size_t len)
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);
	jpeg_mem_src(&cinfo, buf, (unsigned long)len);
	ASSERT_EQUAL(JPEG_HEADER_OK, jpeg_read_header(&cinfo, TRUE));
	jpeg_info_t info;
	info.h0 = cinfo.comp_info[0].h_samp_factor;
	info.v0 = cinfo.comp_info[0].v_samp_factor;
	info.h1 = cinfo.comp_info[1].h_samp_factor;
	info.v1 = cinfo.comp_info[1].v_samp_factor;
	info.progressive = jpeg_has_multiple_scans(&cinfo) != 0;
	jpeg_destroy_decompress(&cinfo);
	return info;
}

static jpeg_info_t jpeg_encode_info(img2_image_t *img, const img2_encode_opts_t *eo)
{
	uint8_t *buf = NULL;
	size_t len = 0;
	ASSERT_EQUAL(IMG2_OK, encode(img, IMG2_FORMAT_JPEG, eo, &buf, &len));
	jpeg_info_t info = jpeg_read_info(buf, len);
	img2_free(buf);
	return info;
}

CTEST(encode_options, jpeg_chroma_progressive_effort)
{
	img2_image_t *img = make_image(48, 32, false);
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;

	jpeg_info_t d = jpeg_encode_info(img, &eo);
	ASSERT_EQUAL(2, d.h0);
	ASSERT_EQUAL(2, d.v0);
	ASSERT_EQUAL(1, d.h1);
	ASSERT_EQUAL(1, d.v1);
	ASSERT_FALSE(d.progressive);

	eo.chroma = IMG2_CHROMA_420;
	assert_same_bytes(img, IMG2_FORMAT_JPEG, NULL, &eo); /* explicit 420 = the default */
	eo.chroma = IMG2_CHROMA_422;
	d = jpeg_encode_info(img, &eo);
	ASSERT_EQUAL(2, d.h0);
	ASSERT_EQUAL(1, d.v0);
	ASSERT_EQUAL(1, d.h1);
	eo.chroma = IMG2_CHROMA_444;
	d = jpeg_encode_info(img, &eo);
	ASSERT_EQUAL(1, d.h0);
	ASSERT_EQUAL(1, d.v0);
	ASSERT_EQUAL(1, d.h1);
	ASSERT_EQUAL(1, d.v1);

	img2_encode_opts_t pr = IMG2_ENCODE_OPTS_INIT;
	pr.progressive = true;
	d = jpeg_encode_info(img, &pr);
	ASSERT_TRUE(d.progressive);
	assert_decodable(img, IMG2_FORMAT_JPEG, &pr);

	/* effort < 6: no change; >= 6: optimised Huffman tables (valid and decodable, and smaller here) */
	img2_encode_opts_t e5 = IMG2_ENCODE_OPTS_INIT;
	e5.effort = 5;
	assert_same_bytes(img, IMG2_FORMAT_JPEG, NULL, &e5);
	img2_encode_opts_t e6 = IMG2_ENCODE_OPTS_INIT;
	e6.effort = 6;
	assert_decodable(img, IMG2_FORMAT_JPEG, &e6);
	e6.effort = 10;
	e6.progressive = true;
	e6.chroma = IMG2_CHROMA_444;
	assert_decodable(img, IMG2_FORMAT_JPEG, &e6);
	img2_image_destroy(img);
}

CTEST(encode_options, jpeg_warnings)
{
	img2_image_t *img = make_image(16, 16, false);
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.lossless = true;
	eo.alpha_quality = 50;
	eo.threads = 4;
	capture_begin();
	assert_decodable(img, IMG2_FORMAT_JPEG, &eo);
	capture_end();
	ASSERT_EQUAL(3, s_warn_count);
	assert_one_warn("encode: lossless has no effect for JPEG (always lossy)");
	assert_one_warn("encode: alpha_quality has no effect for JPEG (no alpha channel)");
	assert_one_warn("encode: threads has no effect for JPEG (single-threaded encoder)");
	img2_image_destroy(img);
}

/* ---- PNG ------------------------------------------------------------------------------------------------ */

CTEST(encode_options, png_interlace_effort_lossless)
{
	img2_image_t *img = make_image(40, 30, true);
	uint8_t *buf = NULL;
	size_t len = 0;

	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	ASSERT_EQUAL(IMG2_OK, encode(img, IMG2_FORMAT_PNG, &eo, &buf, &len));
	ASSERT_TRUE(len > 29);
	ASSERT_EQUAL(0, memcmp(buf + 12, "IHDR", 4));
	ASSERT_EQUAL(0, buf[28]); /* interlace method */
	img2_free(buf);

	eo.progressive = true;
	ASSERT_EQUAL(IMG2_OK, encode(img, IMG2_FORMAT_PNG, &eo, &buf, &len));
	ASSERT_EQUAL(0, memcmp(buf + 12, "IHDR", 4));
	ASSERT_EQUAL(1, buf[28]); /* Adam7 */
	img2_free(buf);
	assert_lossless(img, IMG2_FORMAT_PNG, &eo);

	/* lossless: a no-op without warning (PNG is always lossless) */
	img2_encode_opts_t ll = IMG2_ENCODE_OPTS_INIT;
	ll.lossless = true;
	capture_begin();
	assert_same_bytes(img, IMG2_FORMAT_PNG, NULL, &ll);
	capture_end();
	ASSERT_EQUAL(0, s_warn_count);
	assert_lossless(img, IMG2_FORMAT_PNG, &ll);

	/* effort e with the default quality = zlib level e-1 = quality (e-1)*10 (decision 3) */
	for (int e = 1; e <= 10; e += 3) {
		img2_encode_opts_t ef = IMG2_ENCODE_OPTS_INIT;
		ef.effort = e;
		img2_encode_opts_t lv = IMG2_ENCODE_OPTS_INIT;
		lv.quality = (e - 1) * 10;
		assert_same_bytes(img, IMG2_FORMAT_PNG, &ef, &lv);
		assert_lossless(img, IMG2_FORMAT_PNG, &ef);
	}
	img2_image_destroy(img);
}

CTEST(encode_options, png_warnings)
{
	img2_image_t *img = make_image(16, 16, true);
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.quality = 90;
	eo.effort = 3; /* explicit quality wins */
	eo.chroma = IMG2_CHROMA_444;
	eo.alpha_quality = 50;
	eo.threads = 2;
	capture_begin();
	assert_lossless(img, IMG2_FORMAT_PNG, &eo);
	capture_end();
	ASSERT_EQUAL(4, s_warn_count);
	assert_one_warn("encode: effort has no effect for PNG (an explicit quality selects the zlib level)");
	assert_one_warn("encode: chroma 444 has no effect for PNG (always RGB)");
	assert_one_warn("encode: alpha_quality has no effect for PNG (always lossless)");
	assert_one_warn("encode: threads has no effect for PNG (single-threaded encoder)");

	img2_encode_opts_t q90 = IMG2_ENCODE_OPTS_INIT;
	q90.quality = 90;
	capture_begin();
	assert_same_bytes(img, IMG2_FORMAT_PNG, &q90, &eo);
	capture_end();
	img2_image_destroy(img);
}

/* ---- WebP ----------------------------------------------------------------------------------------------- */

#ifdef IMG2_HAVE_WEBP
CTEST(encode_options, webp_config_lossy_simple_api_lossless_exact)
{
	/* lossy: the WebPConfig path with default options gives the bytes of WebPEncodeRGBA */
	img2_image_t *img = make_image_transparent(40, 28);
	const int qualities[] = { 0, 50, 80, 99 };
	for (size_t i = 0; i < sizeof(qualities) / sizeof(qualities[0]); i++) {
		int q = qualities[i];
		uint8_t *ref = NULL;
		size_t ref_len = WebPEncodeRGBA(img->pixels, (int)img->width, (int)img->height, (int)img->stride, (float)q, &ref);
		ASSERT_TRUE(ref_len > 0);
		uint8_t *buf = NULL;
		size_t len = 0;
		ASSERT_EQUAL(IMG2_OK, img2_encode_webp(img, q, &buf, &len));
		ASSERT_DATA(ref, ref_len, buf, len);
		free(buf);
		WebPFree(ref);
	}

	/* lossless (quality 100 or the option) sets `exact`: unlike WebPEncodeLosslessRGBA, the RGB under alpha 0
	 * survives, every RGBA byte round-trips */
	img2_encode_opts_t q100 = IMG2_ENCODE_OPTS_INIT;
	q100.quality = 100;
	assert_lossless(img, IMG2_FORMAT_WEBP, &q100);
	img2_encode_opts_t ll = IMG2_ENCODE_OPTS_INIT;
	ll.lossless = true;
	assert_lossless(img, IMG2_FORMAT_WEBP, &ll);
	assert_same_bytes(img, IMG2_FORMAT_WEBP, &ll, &q100);
	img2_image_destroy(img);
}

CTEST(encode_options, webp_lossless_effort_alpha_threads)
{
	img2_image_t *img = make_image(40, 28, true);

	img2_encode_opts_t ll = IMG2_ENCODE_OPTS_INIT;
	ll.lossless = true;
	assert_lossless(img, IMG2_FORMAT_WEBP, &ll);
	img2_encode_opts_t q100 = IMG2_ENCODE_OPTS_INIT;
	q100.quality = 100;
	assert_same_bytes(img, IMG2_FORMAT_WEBP, &ll, &q100); /* q=100 still selects lossless, same settings */
	ll.effort = 10;
	assert_lossless(img, IMG2_FORMAT_WEBP, &ll);
	ll.effort = 1;
	assert_lossless(img, IMG2_FORMAT_WEBP, &ll);

	/* effort 7 = method 4 = the default */
	img2_encode_opts_t e7 = IMG2_ENCODE_OPTS_INIT;
	e7.effort = 7;
	assert_same_bytes(img, IMG2_FORMAT_WEBP, NULL, &e7);
	for (int e = 1; e <= 10; e += 9) {
		img2_encode_opts_t ef = IMG2_ENCODE_OPTS_INIT;
		ef.effort = e;
		assert_decodable(img, IMG2_FORMAT_WEBP, &ef);
	}
	img2_encode_opts_t aq = IMG2_ENCODE_OPTS_INIT;
	aq.alpha_quality = 30;
	assert_decodable(img, IMG2_FORMAT_WEBP, &aq);
	img2_encode_opts_t th = IMG2_ENCODE_OPTS_INIT;
	th.threads = 4;
	assert_decodable(img, IMG2_FORMAT_WEBP, &th);
	th.threads = 1; /* thread_level stays 0 */
	assert_same_bytes(img, IMG2_FORMAT_WEBP, NULL, &th);
	img2_encode_opts_t c420 = IMG2_ENCODE_OPTS_INIT;
	c420.chroma = IMG2_CHROMA_420;
	capture_begin();
	assert_same_bytes(img, IMG2_FORMAT_WEBP, NULL, &c420);
	capture_end();
	ASSERT_EQUAL(0, s_warn_count);
	img2_image_destroy(img);
}

CTEST(encode_options, webp_warnings)
{
	img2_image_t *img = make_image(16, 16, true);
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.chroma = IMG2_CHROMA_444;
	eo.progressive = true;
	eo.lossless = true;
	eo.alpha_quality = 50;
	capture_begin();
	assert_lossless(img, IMG2_FORMAT_WEBP, &eo);
	capture_end();
	ASSERT_EQUAL(3, s_warn_count);
	assert_one_warn("encode: chroma 444 has no effect for WebP (always 4:2:0)");
	assert_one_warn("encode: progressive has no effect for WebP (no progressive mode)");
	assert_one_warn("encode: alpha_quality has no effect for lossless WebP (alpha is lossless)");

	img2_encode_opts_t c422 = IMG2_ENCODE_OPTS_INIT;
	c422.chroma = IMG2_CHROMA_422;
	capture_begin();
	assert_decodable(img, IMG2_FORMAT_WEBP, &c422);
	capture_end();
	ASSERT_EQUAL(1, s_warn_count);
	assert_one_warn("encode: chroma 422 has no effect for WebP (always 4:2:0)");
	img2_image_destroy(img);
}
#endif

/* ---- JXL ------------------------------------------------------------------------------------------------ */

#ifdef IMG2_HAVE_JXL
CTEST(encode_options, jxl_lossless_effort_alpha_threads)
{
	img2_image_t *img = make_image(40, 28, true);

	img2_encode_opts_t ll = IMG2_ENCODE_OPTS_INIT;
	ll.lossless = true;
	assert_lossless(img, IMG2_FORMAT_JXL, &ll);
	img2_encode_opts_t q95 = IMG2_ENCODE_OPTS_INIT;
	q95.quality = 95;
	assert_lossless(img, IMG2_FORMAT_JXL, &q95); /* q >= 95 still selects lossless */
	ll.effort = 3;
	assert_lossless(img, IMG2_FORMAT_JXL, &ll);

	img2_encode_opts_t e7 = IMG2_ENCODE_OPTS_INIT;
	e7.effort = 7;
	assert_same_bytes(img, IMG2_FORMAT_JXL, NULL, &e7); /* libjxl default effort */
	for (int e = 1; e <= 10; e += 9) {
		img2_encode_opts_t ef = IMG2_ENCODE_OPTS_INIT;
		ef.effort = e;
		assert_decodable(img, IMG2_FORMAT_JXL, &ef);
	}
	img2_encode_opts_t aq = IMG2_ENCODE_OPTS_INIT;
	aq.alpha_quality = 40;
	assert_decodable(img, IMG2_FORMAT_JXL, &aq);
	aq.alpha_quality = 100; /* lossy colour, lossless alpha (distance 0) */
	img2_frames_t out;
	encode_decode(img, IMG2_FORMAT_JXL, &aq, &out);
	for (uint32_t y = 0; y < img->height; y++) {
		for (uint32_t x = 0; x < img->width; x++) {
			ASSERT_EQUAL(img->pixels[(size_t)y * img->stride + x * 4 + 3], out.frames[0]->pixels[(size_t)y * out.frames[0]->stride + x * 4 + 3]);
		}
	}
	img2_frames_free(&out);

	/* threads 1: no runner (= default bytes); 2+: thread pool, valid and decodable, lossless stays exact */
	img2_encode_opts_t t1 = IMG2_ENCODE_OPTS_INIT;
	t1.threads = 1;
	assert_same_bytes(img, IMG2_FORMAT_JXL, NULL, &t1);
	img2_encode_opts_t t4 = IMG2_ENCODE_OPTS_INIT;
	t4.threads = 4;
	assert_decodable(img, IMG2_FORMAT_JXL, &t4);
	t4.lossless = true;
	assert_lossless(img, IMG2_FORMAT_JXL, &t4);
	img2_image_destroy(img);
}

CTEST(encode_options, jxl_warnings)
{
	img2_image_t *img = make_image(16, 16, true);
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.chroma = IMG2_CHROMA_420;
	eo.progressive = true; /* lossy only (decision 7) */
	eo.lossless = true;
	eo.alpha_quality = 50;
	capture_begin();
	assert_lossless(img, IMG2_FORMAT_JXL, &eo);
	capture_end();
	ASSERT_EQUAL(3, s_warn_count);
	assert_one_warn("encode: chroma 420 has no effect for JXL (no chroma subsampling of RGB / XYB)");
	assert_one_warn("encode: progressive has no effect for lossless JXL (no VarDCT passes)");
	assert_one_warn("encode: alpha_quality has no effect for lossless JXL (alpha is lossless)");
	img2_image_destroy(img);
}

/** JXL_DEC_FRAME_PROGRESSION events (kPasses detail) while the file arrives in 64 chunks (a progressive file can
 *  be shown after each complete pass; with the whole file at once the decoder reports no intermediate step) */
static int jxl_progression_events(const uint8_t *buf, size_t len)
{
	JxlDecoder *dec = JxlDecoderCreate(NULL);
	ASSERT_NOT_NULL(dec);
	ASSERT_EQUAL(JXL_DEC_SUCCESS, JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_FRAME_PROGRESSION | JXL_DEC_FULL_IMAGE));
	ASSERT_EQUAL(JXL_DEC_SUCCESS, JxlDecoderSetProgressiveDetail(dec, kPasses));
	size_t chunk = len / 64 + 1;
	size_t avail = chunk < len ? chunk : len;
	ASSERT_EQUAL(JXL_DEC_SUCCESS, JxlDecoderSetInput(dec, buf, avail));
	if (avail == len) {
		JxlDecoderCloseInput(dec);
	}
	JxlPixelFormat fmt = { 4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
	uint8_t *pixels = NULL;
	int events = 0;
	for (;;) {
		JxlDecoderStatus st = JxlDecoderProcessInput(dec);
		if (st == JXL_DEC_NEED_MORE_INPUT) {
			ASSERT_TRUE(avail < len);
			size_t unread = JxlDecoderReleaseInput(dec);
			size_t start = avail - unread;
			avail = avail + chunk < len ? avail + chunk : len;
			ASSERT_EQUAL(JXL_DEC_SUCCESS, JxlDecoderSetInput(dec, buf + start, avail - start));
			if (avail == len) {
				JxlDecoderCloseInput(dec);
			}
		} else if (st == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
			size_t size = 0;
			ASSERT_EQUAL(JXL_DEC_SUCCESS, JxlDecoderImageOutBufferSize(dec, &fmt, &size));
			pixels = malloc(size);
			ASSERT_NOT_NULL(pixels);
			ASSERT_EQUAL(JXL_DEC_SUCCESS, JxlDecoderSetImageOutBuffer(dec, &fmt, pixels, size));
		} else if (st == JXL_DEC_FRAME_PROGRESSION) {
			events++;
		} else if (st == JXL_DEC_SUCCESS) {
			break;
		} else {
			ASSERT_TRUE(st == JXL_DEC_BASIC_INFO || st == JXL_DEC_FULL_IMAGE);
		}
	}
	free(pixels);
	JxlDecoderDestroy(dec);
	return events;
}

CTEST(encode_options, jxl_progressive)
{
	/* 512x384 photo-like texture (gradient + noise): several AC groups with real AC data */
	img2_image_t *img = make_image(512, 384, false);
	uint32_t seed = 1u;
	for (size_t i = 0; i < (size_t)512 * 384 * 4; i++) {
		if (i % 4 != 3) {
			seed = seed * 1664525u + 1013904223u;
			img->pixels[i] = (uint8_t)((img->pixels[i] * 3 + (seed >> 24)) / 4);
		}
	}
	img2_encode_opts_t pr = IMG2_ENCODE_OPTS_INIT;
	pr.progressive = true;
	capture_begin();
	assert_decodable(img, IMG2_FORMAT_JXL, &pr);
	capture_end();
	ASSERT_EQUAL(0, s_warn_count);

	uint8_t *a = NULL;
	uint8_t *b = NULL;
	size_t la = 0;
	size_t lb = 0;
	ASSERT_EQUAL(IMG2_OK, encode(img, IMG2_FORMAT_JXL, NULL, &a, &la));
	ASSERT_EQUAL(IMG2_OK, encode(img, IMG2_FORMAT_JXL, &pr, &b, &lb));
	ASSERT_FALSE(la == lb && memcmp(a, b, la) == 0);
	int ev_plain = jxl_progression_events(a, la);
	int ev_prog = jxl_progression_events(b, lb);
	ASSERT_TRUE(ev_prog > ev_plain); /* AC passes the decoder can show before the end (goldhill: 1 -> 3) */
	img2_free(a);
	img2_free(b);

	/* lossless: not progressive, one WARN, the bytes of plain lossless */
	pr.lossless = true;
	img2_encode_opts_t ll = IMG2_ENCODE_OPTS_INIT;
	ll.lossless = true;
	capture_begin();
	assert_same_bytes(img, IMG2_FORMAT_JXL, &ll, &pr);
	capture_end();
	ASSERT_EQUAL(1, s_warn_count);
	assert_one_warn("encode: progressive has no effect for lossless JXL (no VarDCT passes)");
	img2_image_destroy(img);
}
#endif

/* ---- HEIF / AVIF ---------------------------------------------------------------------------------------- */

#ifdef IMG2_HAVE_HEIF
CTEST(encode_options, heif_warnings)
{
	img2_image_t *img = make_image(32, 32, true);
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.lossless = true;
	eo.effort = 9;
	eo.chroma = IMG2_CHROMA_422;
	eo.alpha_quality = 50;
	eo.progressive = true;
	eo.threads = 2;
	capture_begin();
	assert_decodable(img, IMG2_FORMAT_HEIF, &eo);
	capture_end();
	ASSERT_EQUAL(6, s_warn_count);
	assert_one_warn("encode: lossless has no effect for HEIF (kvazaar lossless is 4:2:0 YCbCr, not RGB-lossless)");
	assert_one_warn("encode: effort has no effect for HEIF (no kvazaar preset parameter)");
	assert_one_warn("encode: chroma 422 has no effect for HEIF (always 4:2:0)");
	assert_one_warn("encode: alpha_quality has no effect for HEIF (no kvazaar alpha quality parameter)");
	assert_one_warn("encode: progressive has no effect for HEIF (no progressive mode)");
	assert_one_warn("encode: threads has no effect for HEIF (kvazaar chooses the thread count)");

	img2_encode_opts_t c444 = IMG2_ENCODE_OPTS_INIT;
	c444.chroma = IMG2_CHROMA_444;
	capture_begin();
	assert_decodable(img, IMG2_FORMAT_HEIF, &c444);
	capture_end();
	ASSERT_EQUAL(1, s_warn_count);
	assert_one_warn("encode: chroma 444 has no effect for HEIF (always 4:2:0)");

	/* chroma 420 is what kvazaar does: no warning */
	img2_encode_opts_t c420 = IMG2_ENCODE_OPTS_INIT;
	c420.chroma = IMG2_CHROMA_420;
	capture_begin();
	assert_decodable(img, IMG2_FORMAT_HEIF, &c420);
	capture_end();
	ASSERT_EQUAL(0, s_warn_count);
	img2_image_destroy(img);
}

/** chroma of the primary image of an AVIF / HEIF file, as libheif reports it */
static enum heif_chroma heif_file_chroma(img2_image_t *img, const img2_encode_opts_t *eo)
{
	uint8_t *buf = NULL;
	size_t len = 0;
	ASSERT_EQUAL(IMG2_OK, encode(img, IMG2_FORMAT_AVIF, eo, &buf, &len));
	struct heif_context *ctx = heif_context_alloc();
	ASSERT_NOT_NULL(ctx);
	ASSERT_EQUAL(heif_error_Ok, heif_context_read_from_memory_without_copy(ctx, buf, len, NULL).code);
	struct heif_image_handle *h = NULL;
	ASSERT_EQUAL(heif_error_Ok, heif_context_get_primary_image_handle(ctx, &h).code);
	enum heif_colorspace cs = heif_colorspace_undefined;
	enum heif_chroma chroma = heif_chroma_undefined;
	ASSERT_EQUAL(heif_error_Ok, heif_image_handle_get_preferred_decoding_colorspace(h, &cs, &chroma).code);
	heif_image_handle_release(h);
	heif_context_free(ctx);
	img2_free(buf);
	return chroma;
}

CTEST(encode_options, avif_lossless_chroma_effort_threads)
{
	img2_image_t *img = make_image(40, 28, true);

	/* lossless: aom lossless + 4:4:4 + identity matrix: RGB and alpha bit-exact */
	img2_encode_opts_t ll = IMG2_ENCODE_OPTS_INIT;
	ll.lossless = true;
	assert_lossless(img, IMG2_FORMAT_AVIF, &ll);
	ASSERT_EQUAL(heif_chroma_444, heif_file_chroma(img, &ll));
	ll.chroma = IMG2_CHROMA_444; /* explicit 444: no warning */
	capture_begin();
	assert_lossless(img, IMG2_FORMAT_AVIF, &ll);
	capture_end();
	ASSERT_EQUAL(0, s_warn_count);

	img2_encode_opts_t c = IMG2_ENCODE_OPTS_INIT;
	ASSERT_EQUAL(heif_chroma_420, heif_file_chroma(img, &c));
	c.chroma = IMG2_CHROMA_422;
	ASSERT_EQUAL(heif_chroma_422, heif_file_chroma(img, &c));
	c.chroma = IMG2_CHROMA_444;
	ASSERT_EQUAL(heif_chroma_444, heif_file_chroma(img, &c));
	c.chroma = IMG2_CHROMA_420;
	assert_same_bytes(img, IMG2_FORMAT_AVIF, NULL, &c); /* explicit 420 = the libheif default */

	/* effort 4 = speed 6 = the libheif default; 1 and 10: valid and decodable */
	img2_encode_opts_t e4 = IMG2_ENCODE_OPTS_INIT;
	e4.effort = 4;
	assert_same_bytes(img, IMG2_FORMAT_AVIF, NULL, &e4);
	for (int e = 1; e <= 10; e += 9) {
		img2_encode_opts_t ef = IMG2_ENCODE_OPTS_INIT;
		ef.effort = e;
		assert_decodable(img, IMG2_FORMAT_AVIF, &ef);
	}
	img2_encode_opts_t aq = IMG2_ENCODE_OPTS_INIT;
	aq.alpha_quality = 20;
	assert_decodable(img, IMG2_FORMAT_AVIF, &aq);
	img2_encode_opts_t th = IMG2_ENCODE_OPTS_INIT;
	th.threads = 1;
	assert_decodable(img, IMG2_FORMAT_AVIF, &th);
	th.threads = 3;
	assert_decodable(img, IMG2_FORMAT_AVIF, &th);
	img2_image_destroy(img);
}

CTEST(encode_options, avif_warnings)
{
	img2_image_t *img = make_image(16, 16, true);
	img2_encode_opts_t eo = IMG2_ENCODE_OPTS_INIT;
	eo.progressive = true;
	eo.lossless = true;
	eo.chroma = IMG2_CHROMA_420;
	eo.alpha_quality = 50;
	capture_begin();
	assert_lossless(img, IMG2_FORMAT_AVIF, &eo);
	capture_end();
	ASSERT_EQUAL(3, s_warn_count);
	assert_one_warn("encode: progressive has no effect for AVIF (no progressive mode)");
	assert_one_warn("encode: chroma 420 has no effect for lossless AVIF (always 4:4:4)");
	assert_one_warn("encode: alpha_quality has no effect for lossless AVIF (alpha is lossless)");

	img2_encode_opts_t c422 = IMG2_ENCODE_OPTS_INIT;
	c422.lossless = true;
	c422.chroma = IMG2_CHROMA_422;
	capture_begin();
	assert_lossless(img, IMG2_FORMAT_AVIF, &c422);
	capture_end();
	ASSERT_EQUAL(1, s_warn_count);
	assert_one_warn("encode: chroma 422 has no effect for lossless AVIF (always 4:4:4)");
	img2_image_destroy(img);
}
#endif
