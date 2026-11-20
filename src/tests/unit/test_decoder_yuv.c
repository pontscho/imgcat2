/**
 * @file test_decoder_yuv.c
 * @brief Unit tests for the raw YUV decoder, spec parser and MIME routing
 *
 * Every format is exercised on odd-sized frames with distinct per-sample
 * values, minimal and padded strides (padding filled with garbage that must be
 * ignored), lenient last-row sizing, truncated input and invalid parameters.
 * Expected pixels come from a floating-point BT.601 limited-range reference.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/cli.h"
#include "../../imgcat2/core/image.h"
#include "../../imgcat2/core/metadata.h"
#include "../../imgcat2/core/pipeline.h"
#include "../../imgcat2/decoders/decoder.h"
#include "../../imgcat2/decoders/magic.h"
#include "../../imgcat2/decoders/yuv.h"
#include "../ctest.h"

/** Garbage byte written into stride padding and unused samples */
#define PAD_BYTE 0xEE

/* ------------------------------------------------------------------------- */
/* Test helpers                                                              */
/* ------------------------------------------------------------------------- */

static const yuv_format_t ALL_FORMATS[] = { YUV_FMT_I420, YUV_FMT_YV12, YUV_FMT_NV12, YUV_FMT_NV21, YUV_FMT_I422, YUV_FMT_I444, YUV_FMT_YUYV, YUV_FMT_UYVY, YUV_FMT_GRAY };

#define NUM_FORMATS (sizeof(ALL_FORMATS) / sizeof(ALL_FORMATS[0]))

/** Chroma subsampling shifts per format */
static void fmt_shifts(yuv_format_t f, unsigned *xs, unsigned *ys)
{
	*xs = 0;
	*ys = 0;
	switch (f) {
		case YUV_FMT_I420:
		case YUV_FMT_YV12:
		case YUV_FMT_NV12:
		case YUV_FMT_NV21:
			*xs = 1;
			*ys = 1;
			break;
		case YUV_FMT_I422:
		case YUV_FMT_YUYV:
		case YUV_FMT_UYVY: *xs = 1; break;
		default: break;
	}
}

/** Deterministic synthetic sample generators */
static uint8_t gen_y(uint32_t x, uint32_t y)
{
	return (uint8_t)(16 + (x * 37 + y * 53) % 220);
}

static uint8_t gen_u(uint32_t cx, uint32_t cy)
{
	return (uint8_t)(16 + (cx * 71 + cy * 29 + 7) % 225);
}

static uint8_t gen_v(uint32_t cx, uint32_t cy)
{
	return (uint8_t)(16 + (cx * 43 + cy * 97 + 100) % 225);
}

/** Sample source: either the generators above or constant values */
typedef struct {
	bool constant;
	uint8_t y, u, v;
} sample_src_t;

static uint8_t src_y(const sample_src_t *s, uint32_t x, uint32_t y)
{
	return s->constant ? s->y : gen_y(x, y);
}

static uint8_t src_u(const sample_src_t *s, yuv_format_t f, uint32_t cx, uint32_t cy)
{
	if (f == YUV_FMT_GRAY) {
		return 128;
	}
	return s->constant ? s->u : gen_u(cx, cy);
}

static uint8_t src_v(const sample_src_t *s, yuv_format_t f, uint32_t cx, uint32_t cy)
{
	if (f == YUV_FMT_GRAY) {
		return 128;
	}
	return s->constant ? s->v : gen_v(cx, cy);
}

/** Floating-point BT.601 limited-range reference */
static uint8_t ref_clamp(double v)
{
	if (v < 0.0) {
		return 0;
	}
	if (v > 255.0) {
		return 255;
	}
	return (uint8_t)(v + 0.5);
}

static void ref_rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t out[3])
{
	double c = 1.164383 * (y - 16);
	double d = u - 128;
	double e = v - 128;
	out[0] = ref_clamp(c + 1.596027 * e);
	out[1] = ref_clamp(c - 0.391762 * d - 0.812968 * e);
	out[2] = ref_clamp(c + 2.017232 * d);
}

/**
 * @brief Build a frame with the given strides (0 = minimal) and padding garbage
 *
 * The buffer always holds full stride * rows for every plane.
 */
static uint8_t *build_frame(yuv_format_t f, uint32_t w, uint32_t h, size_t ys_in, size_t cs_in, const sample_src_t *s, size_t *out_len)
{
	unsigned xs, yss;
	fmt_shifts(f, &xs, &yss);
	size_t cw = (w + xs) >> xs;
	size_t ch = (h + yss) >> yss;

	bool packed = (f == YUV_FMT_YUYV || f == YUV_FMT_UYVY);
	bool nv = (f == YUV_FMT_NV12 || f == YUV_FMT_NV21);
	size_t y_min = packed ? 4 * cw : w;
	size_t c_min = nv ? 2 * cw : cw;
	size_t y_stride = ys_in ? ys_in : y_min;
	size_t c_stride = cs_in ? cs_in : c_min;
	unsigned c_planes = (packed || f == YUV_FMT_GRAY) ? 0 : (nv ? 1 : 2);

	size_t len = y_stride * h + (size_t)c_planes * c_stride * ch;
	uint8_t *buf = malloc(len);
	memset(buf, PAD_BYTE, len);

	for (uint32_t y = 0; y < h; y++) {
		uint8_t *row = buf + y * y_stride;
		if (packed) {
			unsigned oy0 = (f == YUV_FMT_YUYV) ? 0 : 1, oy1 = oy0 + 2;
			unsigned ou = (f == YUV_FMT_YUYV) ? 1 : 0, ov = ou + 2;
			for (uint32_t m = 0; m < cw; m++) {
				row[m * 4 + oy0] = src_y(s, 2 * m, y);
				if (2 * m + 1 < w) {
					row[m * 4 + oy1] = src_y(s, 2 * m + 1, y);
				}
				row[m * 4 + ou] = src_u(s, f, m, y);
				row[m * 4 + ov] = src_v(s, f, m, y);
			}
		} else {
			for (uint32_t x = 0; x < w; x++) {
				row[x] = src_y(s, x, y);
			}
		}
	}

	uint8_t *c0 = buf + y_stride * h;
	uint8_t *c1 = c0 + c_stride * ch;
	for (uint32_t cy = 0; cy < ch && c_planes > 0; cy++) {
		for (uint32_t cx = 0; cx < cw; cx++) {
			uint8_t u = src_u(s, f, cx, cy), v = src_v(s, f, cx, cy);
			switch (f) {
				case YUV_FMT_NV12:
					c0[cy * c_stride + 2 * cx] = u;
					c0[cy * c_stride + 2 * cx + 1] = v;
					break;
				case YUV_FMT_NV21:
					c0[cy * c_stride + 2 * cx] = v;
					c0[cy * c_stride + 2 * cx + 1] = u;
					break;
				case YUV_FMT_YV12:
					c0[cy * c_stride + cx] = v;
					c1[cy * c_stride + cx] = u;
					break;
				default:
					c0[cy * c_stride + cx] = u;
					c1[cy * c_stride + cx] = v;
					break;
			}
		}
	}

	*out_len = len;
	return buf;
}

/** Compare a decoded image against the reference; returns max abs error */
static int max_error_vs_reference(const image_t *img, yuv_format_t f, const sample_src_t *s)
{
	unsigned xs, yss;
	fmt_shifts(f, &xs, &yss);
	int max_err = 0;
	for (uint32_t y = 0; y < img->height; y++) {
		for (uint32_t x = 0; x < img->width; x++) {
			uint8_t exp[3];
			ref_rgb(src_y(s, x, y), src_u(s, f, x >> xs, y >> yss), src_v(s, f, x >> xs, y >> yss), exp);
			const uint8_t *px = img->pixels + ((size_t)y * img->width + x) * 4;
			for (int c = 0; c < 3; c++) {
				int d = abs((int)px[c] - (int)exp[c]);
				if (d > max_err) {
					max_err = d;
				}
			}
			if (px[3] != 255) {
				return 1000;
			}
		}
	}
	return max_err;
}

/** Decode a frame and return max error (or -1 on decode failure) */
static int decode_and_check(yuv_format_t f, uint32_t w, uint32_t h, size_t ys, size_t cs, const sample_src_t *s)
{
	size_t len;
	uint8_t *buf = build_frame(f, w, h, ys, cs, s, &len);
	yuv_params_t p = { .format = f, .width = w, .height = h, .y_stride = ys, .uv_stride = cs };
	int fc = -1;
	image_t **frames = decode_yuv(buf, len, &p, &fc);
	free(buf);
	if (frames == NULL || fc != 1 || frames[0]->width != w || frames[0]->height != h) {
		decoder_free_frames(frames, fc);
		return -1;
	}
	int err = max_error_vs_reference(frames[0], f, s);
	decoder_free_frames(frames, fc);
	return err;
}

/* ------------------------------------------------------------------------- */
/* Format names                                                              */
/* ------------------------------------------------------------------------- */

CTEST(decoder_yuv, format_names_and_aliases)
{
	ASSERT_EQUAL(YUV_FMT_I420, yuv_format_from_name("i420"));
	ASSERT_EQUAL(YUV_FMT_I420, yuv_format_from_name("YUV420P"));
	ASSERT_EQUAL(YUV_FMT_YV12, yuv_format_from_name("Yv12"));
	ASSERT_EQUAL(YUV_FMT_NV12, yuv_format_from_name("NV12"));
	ASSERT_EQUAL(YUV_FMT_NV21, yuv_format_from_name("nv21"));
	ASSERT_EQUAL(YUV_FMT_I422, yuv_format_from_name("yuv422p"));
	ASSERT_EQUAL(YUV_FMT_I422, yuv_format_from_name("I422"));
	ASSERT_EQUAL(YUV_FMT_I444, yuv_format_from_name("yuv444P"));
	ASSERT_EQUAL(YUV_FMT_I444, yuv_format_from_name("i444"));
	ASSERT_EQUAL(YUV_FMT_YUYV, yuv_format_from_name("YUY2"));
	ASSERT_EQUAL(YUV_FMT_YUYV, yuv_format_from_name("yuyv"));
	ASSERT_EQUAL(YUV_FMT_UYVY, yuv_format_from_name("UYVY"));
	ASSERT_EQUAL(YUV_FMT_GRAY, yuv_format_from_name("gray"));
	ASSERT_EQUAL(YUV_FMT_GRAY, yuv_format_from_name("Y8"));
	ASSERT_EQUAL(YUV_FMT_GRAY, yuv_format_from_name("y800"));

	ASSERT_EQUAL(YUV_FMT_NONE, yuv_format_from_name("rgb24"));
	ASSERT_EQUAL(YUV_FMT_NONE, yuv_format_from_name(""));
	ASSERT_EQUAL(YUV_FMT_NONE, yuv_format_from_name("i42"));
	ASSERT_EQUAL(YUV_FMT_NONE, yuv_format_from_name("i4200"));
	ASSERT_EQUAL(YUV_FMT_NONE, yuv_format_from_name(NULL));

	ASSERT_STR("i420", yuv_format_name(YUV_FMT_I420));
	ASSERT_STR("yuyv", yuv_format_name(YUV_FMT_YUYV));
	ASSERT_STR("gray", yuv_format_name(YUV_FMT_GRAY));
	ASSERT_STR("unknown", yuv_format_name(YUV_FMT_NONE));
}

/* ------------------------------------------------------------------------- */
/* Spec parsing                                                              */
/* ------------------------------------------------------------------------- */

CTEST(decoder_yuv, parse_spec_valid)
{
	yuv_params_t p;
	char err[160];

	ASSERT_EQUAL(0, yuv_parse_spec("i420:3:3", &p, err, sizeof(err)));
	ASSERT_EQUAL(YUV_FMT_I420, p.format);
	ASSERT_EQUAL(3, p.width);
	ASSERT_EQUAL(3, p.height);
	ASSERT_EQUAL(3, p.y_stride);
	ASSERT_EQUAL(2, p.uv_stride);

	ASSERT_EQUAL(0, yuv_parse_spec("NV12:3:3", &p, err, sizeof(err)));
	ASSERT_EQUAL(YUV_FMT_NV12, p.format);
	ASSERT_EQUAL(3, p.y_stride);
	ASSERT_EQUAL(4, p.uv_stride);

	ASSERT_EQUAL(0, yuv_parse_spec("yuy2:3:2", &p, err, sizeof(err)));
	ASSERT_EQUAL(YUV_FMT_YUYV, p.format);
	ASSERT_EQUAL(8, p.y_stride);
	ASSERT_EQUAL(0, p.uv_stride);

	ASSERT_EQUAL(0, yuv_parse_spec("i420:4:4:8:6", &p, err, sizeof(err)));
	ASSERT_EQUAL(8, p.y_stride);
	ASSERT_EQUAL(6, p.uv_stride);

	/* Empty optional field = default */
	ASSERT_EQUAL(0, yuv_parse_spec("nv21:4:4::8", &p, err, sizeof(err)));
	ASSERT_EQUAL(4, p.y_stride);
	ASSERT_EQUAL(8, p.uv_stride);

	ASSERT_EQUAL(0, yuv_parse_spec("i444:5:1:16", &p, err, sizeof(err)));
	ASSERT_EQUAL(16, p.y_stride);
	ASSERT_EQUAL(5, p.uv_stride);

	ASSERT_EQUAL(0, yuv_parse_spec("Y800:1:1", &p, err, sizeof(err)));
	ASSERT_EQUAL(YUV_FMT_GRAY, p.format);

	ASSERT_EQUAL(0, yuv_parse_spec("i422:65536:2", &p, err, sizeof(err)));
	ASSERT_EQUAL(65536, p.width);
}

CTEST(decoder_yuv, parse_spec_invalid)
{
	yuv_params_t p = { 0 };
	char err[160];

	static const char *bad[] = {
		"",
		"i420",
		"i420:3",
		"i420:3:3:3:2:9", /* too many fields */
		":3:3", /* missing format */
		"rgb24:3:3", /* unknown format */
		"i420:0:3", /* zero width */
		"i420:3:0", /* zero height */
		"i420::3", /* empty width */
		"i420:3:", /* empty height */
		"i420:3x:3", /* garbage */
		"i420:abc:3",
		"i420:-3:3", /* sign */
		"i420:+3:3",
		"i420: 3:3", /* whitespace */
		"i420:0x10:3", /* hex */
		"i420:99999999999999999999:3", /* numeric overflow */
		"i420:4294967296:3", /* > UINT32_MAX */
		"i420:65537:2", /* > IMAGE_MAX_DIMENSION */
		"i420:65536:65536", /* > IMAGE_MAX_PIXELS */
		"i420:4:4:3", /* y_stride < 4 */
		"i420:4:4:4:1", /* uv_stride < 2 */
		"nv12:3:3:3:3", /* uv_stride < 4 */
		"yuyv:3:2:6", /* y_stride < 8 (odd width => 2 macropixels) */
		"yuyv:2:2:4:2", /* uv_stride not applicable */
		"gray:2:2:2:2", /* uv_stride not applicable */
		"i420:4:4:0", /* zero stride */
		"i420:4:4:4:0",
		"i420:4:4:18446744073709551615", /* stride * rows overflows size_t */
		"i420:4:4:4:18446744073709551615",
		"i420:4:4:99999999999999999999999", /* stride numeric overflow */
	};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		err[0] = '\0';
		int rc = yuv_parse_spec(bad[i], &p, err, sizeof(err));
		if (rc == 0) {
			fprintf(stderr, "unexpectedly accepted: '%s'\n", bad[i]);
		}
		ASSERT_EQUAL(-1, rc);
		ASSERT_TRUE(err[0] != '\0'); /* a message was produced */
	}

	/* Over-long spec */
	char longspec[300];
	memset(longspec, '1', sizeof(longspec) - 1);
	longspec[sizeof(longspec) - 1] = '\0';
	memcpy(longspec, "i420:", 5);
	ASSERT_EQUAL(-1, yuv_parse_spec(longspec, &p, err, sizeof(err)));

	/* NULL handling, NULL error buffer allowed */
	ASSERT_EQUAL(-1, yuv_parse_spec(NULL, &p, err, sizeof(err)));
	ASSERT_EQUAL(-1, yuv_parse_spec("i420:2:2", NULL, err, sizeof(err)));
	ASSERT_EQUAL(-1, yuv_parse_spec("bogus:2:2", &p, NULL, 0));
	ASSERT_EQUAL(0, yuv_parse_spec("i420:2:2", &p, NULL, 0));

	/* Specific messages */
	yuv_parse_spec("i420:4:4:3", &p, err, sizeof(err));
	ASSERT_NOT_NULL(strstr(err, "y_stride 3"));
	yuv_parse_spec("foo:4:4", &p, err, sizeof(err));
	ASSERT_NOT_NULL(strstr(err, "unknown format 'foo'"));
}

CTEST(decoder_yuv, frame_size)
{
	size_t sz = 0;
	yuv_params_t p = { .format = YUV_FMT_I420, .width = 3, .height = 3 };
	ASSERT_TRUE(yuv_frame_size(&p, &sz));
	ASSERT_EQUAL(9 + 4 + 4, (int)sz);

	/* Padded strides: Y 8*4=32, U 6*2=12, V at 44 + one stride + 2 visible bytes */
	p = (yuv_params_t){ .format = YUV_FMT_I420, .width = 4, .height = 4, .y_stride = 8, .uv_stride = 6 };
	ASSERT_TRUE(yuv_frame_size(&p, &sz));
	ASSERT_EQUAL(44 + 6 + 2, (int)sz);

	/* Packed, lenient last row: 10 + 8 */
	p = (yuv_params_t){ .format = YUV_FMT_YUYV, .width = 3, .height = 2, .y_stride = 10 };
	ASSERT_TRUE(yuv_frame_size(&p, &sz));
	ASSERT_EQUAL(18, (int)sz);

	/* NV12 3x3: 9 + 2 rows of 4 bytes */
	p = (yuv_params_t){ .format = YUV_FMT_NV12, .width = 3, .height = 3 };
	ASSERT_TRUE(yuv_frame_size(&p, &sz));
	ASSERT_EQUAL(17, (int)sz);

	/* I422 3x3: 9 + 3*2 + 3*2 */
	p = (yuv_params_t){ .format = YUV_FMT_I422, .width = 3, .height = 3 };
	ASSERT_TRUE(yuv_frame_size(&p, &sz));
	ASSERT_EQUAL(21, (int)sz);

	/* I444 3x3 / gray 3x3 */
	p = (yuv_params_t){ .format = YUV_FMT_I444, .width = 3, .height = 3 };
	ASSERT_TRUE(yuv_frame_size(&p, &sz));
	ASSERT_EQUAL(27, (int)sz);
	p = (yuv_params_t){ .format = YUV_FMT_GRAY, .width = 3, .height = 3 };
	ASSERT_TRUE(yuv_frame_size(&p, &sz));
	ASSERT_EQUAL(9, (int)sz);

	/* Invalid */
	p = (yuv_params_t){ .format = YUV_FMT_NONE, .width = 3, .height = 3 };
	ASSERT_FALSE(yuv_frame_size(&p, &sz));
	p = (yuv_params_t){ .format = YUV_FMT_I420, .width = 4, .height = 4, .y_stride = SIZE_MAX };
	ASSERT_FALSE(yuv_frame_size(&p, &sz));
	ASSERT_FALSE(yuv_frame_size(NULL, &sz));
}

/* ------------------------------------------------------------------------- */
/* Decoding                                                                  */
/* ------------------------------------------------------------------------- */

/** Decode a constant-color 3x3 frame and return the first pixel */
static bool decode_constant(yuv_format_t f, uint8_t y, uint8_t u, uint8_t v, uint8_t out[4])
{
	sample_src_t s = { .constant = true, .y = y, .u = u, .v = v };
	size_t len;
	uint8_t *buf = build_frame(f, 3, 3, 0, 0, &s, &len);
	yuv_params_t p = { .format = f, .width = 3, .height = 3 };
	int fc = 0;
	image_t **frames = decode_yuv(buf, len, &p, &fc);
	free(buf);
	if (frames == NULL) {
		return false;
	}
	/* All pixels must be identical */
	bool uniform = true;
	for (size_t i = 1; i < 9; i++) {
		if (memcmp(frames[0]->pixels, frames[0]->pixels + i * 4, 4) != 0) {
			uniform = false;
		}
	}
	memcpy(out, frames[0]->pixels, 4);
	decoder_free_frames(frames, fc);
	return uniform;
}

CTEST(decoder_yuv, known_colors_all_formats)
{
	for (size_t i = 0; i < NUM_FORMATS; i++) {
		yuv_format_t f = ALL_FORMATS[i];
		uint8_t px[4];

		/* Black */
		ASSERT_TRUE(decode_constant(f, 16, 128, 128, px));
		ASSERT_EQUAL(0, px[0]);
		ASSERT_EQUAL(0, px[1]);
		ASSERT_EQUAL(0, px[2]);
		ASSERT_EQUAL(255, px[3]);

		/* White */
		ASSERT_TRUE(decode_constant(f, 235, 128, 128, px));
		ASSERT_EQUAL(255, px[0]);
		ASSERT_EQUAL(255, px[1]);
		ASSERT_EQUAL(255, px[2]);
		ASSERT_EQUAL(255, px[3]);

		/* Below black / above white clamp */
		ASSERT_TRUE(decode_constant(f, 0, 128, 128, px));
		ASSERT_EQUAL(0, px[0]);
		ASSERT_TRUE(decode_constant(f, 255, 128, 128, px));
		ASSERT_EQUAL(255, px[0]);

		if (f == YUV_FMT_GRAY) {
			/* Mid gray: 1.164383 * (126 - 16) = 128.08 */
			ASSERT_TRUE(decode_constant(f, 126, 0, 0, px));
			ASSERT_EQUAL(128, px[0]);
			ASSERT_EQUAL(128, px[1]);
			ASSERT_EQUAL(128, px[2]);
			continue;
		}

		/* BT.601 limited pure red (81, 90, 240) */
		ASSERT_TRUE(decode_constant(f, 81, 90, 240, px));
		ASSERT_INTERVAL(250, 255, px[0]);
		ASSERT_INTERVAL(0, 3, px[1]);
		ASSERT_INTERVAL(0, 3, px[2]);

		/* Pure green (145, 54, 34) */
		ASSERT_TRUE(decode_constant(f, 145, 54, 34, px));
		ASSERT_INTERVAL(0, 3, px[0]);
		ASSERT_INTERVAL(250, 255, px[1]);
		ASSERT_INTERVAL(0, 3, px[2]);

		/* Pure blue (41, 240, 110) */
		ASSERT_TRUE(decode_constant(f, 41, 240, 110, px));
		ASSERT_INTERVAL(0, 3, px[0]);
		ASSERT_INTERVAL(0, 3, px[1]);
		ASSERT_INTERVAL(250, 255, px[2]);
	}
}

CTEST(decoder_yuv, per_sample_layout_minimal_strides)
{
	sample_src_t s = { .constant = false };
	static const uint32_t sizes[][2] = {
		{ 3, 3 },
		{ 1, 1 },
		{ 5, 4 },
		{ 4, 5 },
		{ 7, 1 },
		{ 1, 7 },
	};
	for (size_t i = 0; i < NUM_FORMATS; i++) {
		for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
			int err = decode_and_check(ALL_FORMATS[i], sizes[k][0], sizes[k][1], 0, 0, &s);
			if (err != 0) {
				fprintf(stderr, "format %s %ux%u: max error %d\n", yuv_format_name(ALL_FORMATS[i]), sizes[k][0], sizes[k][1], err);
			}
			/* Fixed-point vs float reference: allow 1 LSB */
			ASSERT_INTERVAL(0, 1, err);
		}
	}
}

CTEST(decoder_yuv, per_sample_layout_padded_strides)
{
	sample_src_t s = { .constant = false };
	for (size_t i = 0; i < NUM_FORMATS; i++) {
		yuv_format_t f = ALL_FORMATS[i];
		bool has_chroma = !(f == YUV_FMT_YUYV || f == YUV_FMT_UYVY || f == YUV_FMT_GRAY);
		bool packed = (f == YUV_FMT_YUYV || f == YUV_FMT_UYVY);
		/* 5x3: minimal y = 5 (packed: 12), chroma min <= 10 */
		size_t ys = packed ? 12 + 7 : 5 + 3;
		size_t cs = has_chroma ? 13 : 0;
		int err = decode_and_check(f, 5, 3, ys, cs, &s);
		if (err != 0) {
			fprintf(stderr, "format %s padded: max error %d\n", yuv_format_name(f), err);
		}
		ASSERT_INTERVAL(0, 1, err);
	}
}

CTEST(decoder_yuv, lenient_last_row_and_truncation)
{
	sample_src_t s = { .constant = false };
	for (size_t i = 0; i < NUM_FORMATS; i++) {
		yuv_format_t f = ALL_FORMATS[i];
		bool packed = (f == YUV_FMT_YUYV || f == YUV_FMT_UYVY);
		bool has_chroma = !(packed || f == YUV_FMT_GRAY);
		yuv_params_t p = { .format = f, .width = 5, .height = 3, .y_stride = packed ? 20 : 9, .uv_stride = has_chroma ? 14 : 0 };

		size_t len;
		uint8_t *buf = build_frame(f, 5, 3, p.y_stride, p.uv_stride, &s, &len);

		size_t need = 0;
		ASSERT_TRUE(yuv_frame_size(&p, &need));
		ASSERT_TRUE(need < len); /* lenient: last row's padding not required */

		/* Exactly the lenient size decodes */
		int fc = 0;
		image_t **frames = decode_yuv(buf, need, &p, &fc);
		ASSERT_NOT_NULL(frames);
		ASSERT_EQUAL(1, fc);
		ASSERT_INTERVAL(0, 1, max_error_vs_reference(frames[0], f, &s));
		decoder_free_frames(frames, fc);

		/* One byte short fails */
		fc = 42;
		frames = decode_yuv(buf, need - 1, &p, &fc);
		ASSERT_NULL(frames);
		ASSERT_EQUAL(0, fc);

		/* Way short fails */
		frames = decode_yuv(buf, 1, &p, &fc);
		ASSERT_NULL(frames);

		free(buf);
	}
}

CTEST(decoder_yuv, trailing_data_first_frame_only)
{
	sample_src_t s = { .constant = false };
	size_t len;
	uint8_t *frame = build_frame(YUV_FMT_I420, 3, 3, 0, 0, &s, &len);

	/* Two frames back to back, the second one garbage */
	uint8_t *buf = malloc(len * 2);
	memcpy(buf, frame, len);
	memset(buf + len, 0x00, len);

	yuv_params_t p = { .format = YUV_FMT_I420, .width = 3, .height = 3 };
	int fc = 0;
	image_t **frames = decode_yuv(buf, len * 2, &p, &fc);
	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, fc);
	ASSERT_INTERVAL(0, 1, max_error_vs_reference(frames[0], YUV_FMT_I420, &s));
	decoder_free_frames(frames, fc);

	free(buf);
	free(frame);
}

CTEST(decoder_yuv, invalid_parameters)
{
	uint8_t data[64];
	memset(data, 128, sizeof(data));
	int fc = 7;

	yuv_params_t ok = { .format = YUV_FMT_I420, .width = 2, .height = 2 };
	ASSERT_NULL(decode_yuv(NULL, sizeof(data), &ok, &fc));
	ASSERT_EQUAL(0, fc);
	ASSERT_NULL(decode_yuv(data, 0, &ok, &fc));
	ASSERT_NULL(decode_yuv(data, sizeof(data), NULL, &fc));
	ASSERT_NULL(decode_yuv(data, sizeof(data), &ok, NULL));

	yuv_params_t bad = ok;
	bad.format = YUV_FMT_NONE;
	ASSERT_NULL(decode_yuv(data, sizeof(data), &bad, &fc));

	bad = ok;
	bad.width = 0;
	ASSERT_NULL(decode_yuv(data, sizeof(data), &bad, &fc));

	bad = ok;
	bad.height = 0;
	ASSERT_NULL(decode_yuv(data, sizeof(data), &bad, &fc));

	bad = ok;
	bad.width = IMAGE_MAX_DIMENSION + 1;
	ASSERT_NULL(decode_yuv(data, sizeof(data), &bad, &fc));

	bad = ok;
	bad.y_stride = 1; /* < width */
	ASSERT_NULL(decode_yuv(data, sizeof(data), &bad, &fc));

	bad = ok;
	bad.uv_stride = SIZE_MAX; /* overflow */
	ASSERT_NULL(decode_yuv(data, sizeof(data), &bad, &fc));

	bad = (yuv_params_t){ .format = YUV_FMT_GRAY, .width = 2, .height = 2, .uv_stride = 2 };
	ASSERT_NULL(decode_yuv(data, sizeof(data), &bad, &fc));

	/* Sanity: the valid params decode */
	image_t **frames = decode_yuv(data, sizeof(data), &ok, &fc);
	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, fc);
	decoder_free_frames(frames, fc);
}

/* ------------------------------------------------------------------------- */
/* MIME routing                                                              */
/* ------------------------------------------------------------------------- */

CTEST(decoder_yuv, mime_names)
{
	ASSERT_STR("YUV", mime_type_name(MIME_YUV));
	ASSERT_STR("application/octet-stream", get_mime_string(MIME_YUV));

	/* Magic detection never yields MIME_YUV */
	uint8_t buf[64];
	for (int v = 0; v < 256; v += 17) {
		memset(buf, v, sizeof(buf));
		ASSERT_NOT_EQUAL(MIME_YUV, detect_mime_type(buf, sizeof(buf)));
	}
}

CTEST(decoder_yuv, decoder_decode_requires_yuv_options)
{
	uint8_t data[16];
	memset(data, 16, sizeof(data));
	int fc = 0;

	decoder_registry_init(NULL);

	/* No options: refused */
	ASSERT_NULL(decoder_decode(NULL, data, sizeof(data), MIME_YUV, &fc));

	cli_options_t opts = { .silent = true };
	ASSERT_NULL(decoder_decode(&opts, data, sizeof(data), MIME_YUV, &fc));

	/* With --yuv geometry */
	opts.yuv_input = true;
	ASSERT_EQUAL(0, yuv_parse_spec("gray:4:4", &opts.yuv, NULL, 0));
	image_t **frames = decoder_decode(&opts, data, sizeof(data), MIME_YUV, &fc);
	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, fc);
	ASSERT_EQUAL(0, frames[0]->pixels[0]);
	decoder_free_frames(frames, fc);
}

CTEST(decoder_yuv, pipeline_bypasses_magic_detection)
{
	/* Data that starts with a PNG signature must be decoded as raw YUV with --yuv */
	uint8_t data[16] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 16, 16, 16, 16, 16, 16, 16, 16 };

	decoder_registry_init(NULL);

	cli_options_t opts = { .silent = true, .yuv_input = true };
	ASSERT_EQUAL(0, yuv_parse_spec("y8:4:4", &opts.yuv, NULL, 0));
	ASSERT_EQUAL(MIME_YUV, pipeline_input_mime(&opts, data, sizeof(data)));

	image_t **frames = NULL;
	int fc = 0;
	ASSERT_EQUAL(0, pipeline_decode(&opts, data, sizeof(data), &frames, &fc));
	ASSERT_EQUAL(1, fc);
	ASSERT_EQUAL(4, frames[0]->width);
	ASSERT_EQUAL(4, frames[0]->height);
	/* Last pixel came from Y=16 -> black */
	ASSERT_EQUAL(0, frames[0]->pixels[15 * 4]);
	decoder_free_frames(frames, fc);

	/* Without --yuv the same buffer is detected by magic */
	opts.yuv_input = false;
	ASSERT_EQUAL(MIME_PNG, pipeline_input_mime(&opts, data, sizeof(data)));
	ASSERT_EQUAL(MIME_PNG, pipeline_input_mime(NULL, data, sizeof(data)));

	/* Too short for the declared geometry: pipeline fails */
	opts.yuv_input = true;
	ASSERT_EQUAL(0, yuv_parse_spec("i420:8:8", &opts.yuv, NULL, 0));
	frames = NULL;
	ASSERT_NOT_EQUAL(0, pipeline_decode(&opts, data, sizeof(data), &frames, &fc));
	ASSERT_NULL(frames);
}
