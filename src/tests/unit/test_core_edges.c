/**
 * @file test_core_edges.c
 * @brief Edge cases of the core modules (image, api, decoder/encoder registries, log, magic)
 *
 * Argument checks, limit paths and rarely taken branches that the format tests do not reach
 * (docs/coverage.md). Fixtures are built in memory.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <img2/img2.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/core/log.h"
#include "../../libimg2/decoders/decoder.h"
#include "../../libimg2/decoders/magic.h"
#include "../../libimg2/encoders/encoder.h"
#include "../ctest.h"

typedef struct {
	int calls;
	char last[1024];
} edge_log_t;

static void edge_log_cb(img2_log_level_t level, const char *msg, void *user)
{
	(void)level;
	edge_log_t *cap = (edge_log_t *)user;
	cap->calls++;
	snprintf(cap->last, sizeof(cap->last), "%s", msg);
}

static img2_image_t *edge_image(uint32_t w, uint32_t h)
{
	img2_image_t *img = img2_image_alloc(w, h);
	if (img != NULL) {
		for (size_t i = 0; i < (size_t)w * h * 4; i++) {
			img->pixels[i] = (uint8_t)(i * 7);
		}
	}
	return img;
}

/* ===== image.c ===== */

CTEST(core_edges, scale_internal_argument_checks)
{
	img2_image_t *out = (img2_image_t *)1;
	img2_image_t *src = edge_image(4, 2);
	ASSERT_NOT_NULL(src);

	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_resize(src, 2, 2, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_resize(NULL, 2, 2, &out));
	ASSERT_NULL(out);
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_resize(src, 0, 2, &out));
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_resize(src, 2, 0, &out));
	/* target beyond the dimension limit: the output allocation is refused as a limit */
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_image_scale_resize(src, IMG2_MAX_DIMENSION + 1, 1, &out));

	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_fit(src, 2, 2, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_fit(NULL, 2, 2, &out));
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_fit(src, 0, 2, &out));
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_fit(src, 2, 0, &out));

	/* source without dimensions (internal entry point does not validate): img2_fit_size rejects it */
	img2_image_t bogus = *src;
	bogus.width = 0;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_fit(&bogus, 2, 2, &out));

	img2_image_destroy(src);
}

CTEST(core_edges, scale_public_argument_checks)
{
	img2_image_t *out = NULL;
	img2_image_t *src = edge_image(3, 3);
	ASSERT_NOT_NULL(src);

	ASSERT_EQUAL(IMG2_E_ARG, img2_scale(src, 2, 2, IMG2_FILTER_BOX, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_scale_fit(src, 2, 2, IMG2_FILTER_BOX, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_scale(NULL, 2, 2, IMG2_FILTER_BOX, &out));
	ASSERT_EQUAL(IMG2_E_ARG, img2_scale(src, 2, 2, (img2_filter_t)99, &out));
	ASSERT_EQUAL(IMG2_E_ARG, img2_scale_fit(src, 2, 2, (img2_filter_t)99, &out));

	/* every filter */
	const img2_filter_t filters[] = { IMG2_FILTER_DEFAULT, IMG2_FILTER_POINT, IMG2_FILTER_BOX, IMG2_FILTER_TRIANGLE, IMG2_FILTER_CUBIC, IMG2_FILTER_MITCHELL };
	for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); i++) {
		ASSERT_EQUAL(IMG2_OK, img2_scale(src, 5, 4, filters[i], &out));
		ASSERT_EQUAL(5, out->width);
		img2_image_destroy(out);
		ASSERT_EQUAL(IMG2_OK, img2_scale_fit(src, 6, 2, filters[i], &out));
		img2_image_destroy(out);
	}
	img2_image_destroy(src);
}

CTEST(core_edges, validate_rejects_bad_geometry)
{
	img2_image_t *img = edge_image(2, 2);
	ASSERT_NOT_NULL(img);
	img2_image_t probe = *img;

	ASSERT_EQUAL(IMG2_E_ARG, img2_image_validate(NULL));
	probe.width = IMG2_MAX_DIMENSION + 1;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_validate(&probe));
	probe = *img;
	probe.height = IMG2_MAX_DIMENSION + 1;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_validate(&probe));
	probe = *img;
	probe.height = 0;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_validate(&probe));
	/* both axes within the limit, pixel count above IMG2_MAX_PIXELS */
	probe = *img;
	probe.width = IMG2_MAX_DIMENSION;
	probe.height = IMG2_MAX_DIMENSION;
	probe.stride = IMG2_MAX_DIMENSION * 4;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_validate(&probe));
	/* stride below the row size, padded stride */
	probe = *img;
	probe.stride = 4;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_validate(&probe));
	probe.stride = 12;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_validate(&probe));

	/* frames */
	ASSERT_EQUAL(IMG2_E_ARG, img2_frames_validate(NULL));
	img2_frames_t f = { NULL, -1, 0 };
	ASSERT_EQUAL(IMG2_E_ARG, img2_frames_validate(&f));
	f.count = 1;
	ASSERT_EQUAL(IMG2_E_ARG, img2_frames_validate(&f));
	img2_image_t *arr[1] = { &probe };
	f.frames = arr;
	ASSERT_EQUAL(IMG2_E_ARG, img2_frames_validate(&f));
	arr[0] = img;
	ASSERT_EQUAL(IMG2_OK, img2_frames_validate(&f));
	f.count = 0;
	f.frames = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_frames_validate(&f));

	img2_frames_free(NULL);
	img2_image_destroy(img);
}

CTEST(core_edges, create_and_convert_arguments)
{
	img2_image_t *out = NULL;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_create(0, 1, &out));
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_create(1, 0, &out));
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_image_create(IMG2_MAX_DIMENSION + 1, 1, &out));

	const uint8_t px[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
	ASSERT_EQUAL(IMG2_E_ARG, img2_convert_rgb_to_rgba(px, 1, 1, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_convert_gray_to_rgba(px, 1, 1, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_convert_rgb_to_rgba(NULL, 1, 1, &out));
	ASSERT_EQUAL(IMG2_E_ARG, img2_convert_gray_to_rgba(NULL, 1, 1, &out));
	/* dimensions above the limit: LIMIT, not NOMEM */
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_convert_rgb_to_rgba(px, IMG2_MAX_DIMENSION + 1, 1, &out));
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_convert_gray_to_rgba(px, IMG2_MAX_DIMENSION + 1, 1, &out));
	ASSERT_NULL(out);

	ASSERT_EQUAL(IMG2_OK, img2_convert_gray_to_rgba(px, 2, 2, &out));
	ASSERT_EQUAL(3, out->pixels[8]);
	ASSERT_EQUAL(255, out->pixels[11]);
	img2_image_destroy(out);
}

CTEST(core_edges, orient_every_value)
{
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_orient(NULL, 2));
	img2_image_t nopix = { 0 };
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_orient(&nopix, 2));

	for (uint16_t o = 0; o <= 9; o++) {
		img2_image_t *img = edge_image(3, 2);
		ASSERT_NOT_NULL(img);
		img2_status_t st = img2_image_orient(img, o);
		if (o < 1 || o > 8) {
			ASSERT_EQUAL(IMG2_E_ARG, st);
		} else {
			ASSERT_EQUAL(IMG2_OK, st);
			ASSERT_EQUAL((o >= 5) ? 2u : 3u, img->width);
		}
		img2_image_destroy(img);
	}
}

/* ===== api.c ===== */

CTEST(core_edges, encode_argument_checks)
{
	img2_image_t *img = edge_image(2, 2);
	ASSERT_NOT_NULL(img);
	img2_image_t *arr[1] = { img };
	img2_frames_t in = { arr, 1, 0 };
	uint8_t *buf = (uint8_t *)1;
	size_t len = 1;

	ASSERT_EQUAL(IMG2_E_ARG, img2_encode(&in, IMG2_FORMAT_PNG, NULL, NULL, &len));
	ASSERT_EQUAL(0, len);
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode(&in, IMG2_FORMAT_PNG, NULL, &buf, NULL));
	ASSERT_NULL(buf);
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode(NULL, IMG2_FORMAT_PNG, NULL, &buf, &len));
	img2_frames_t empty = { arr, 0, 0 };
	ASSERT_EQUAL(IMG2_E_ARG, img2_encode(&empty, IMG2_FORMAT_PNG, NULL, &buf, &len));

	/* a format value outside the enum: no encoder */
	ASSERT_EQUAL(IMG2_E_UNSUPPORTED, img2_encode(&in, (img2_format_t)77, NULL, &buf, &len));
	ASSERT_FALSE(img2_can_encode((img2_format_t)77));
	ASSERT_NULL(img2_encoder_info(img2_encoder_count()));

	/* PNG quality 100 maps to zlib level 9 (capped); NULL opts = default */
	img2_encode_opts_t q = { 100 };
	ASSERT_EQUAL(IMG2_OK, img2_encode(&in, IMG2_FORMAT_PNG, &q, &buf, &len));
	img2_free(buf);
	ASSERT_EQUAL(IMG2_OK, img2_encode(&in, IMG2_FORMAT_PNG, NULL, &buf, &len));
	img2_free(buf);

	img2_image_destroy(img);
}

CTEST(core_edges, encoder_dispatch_paths)
{
	uint8_t *buf = NULL;
	size_t len = 0;
	img2_image_t *img = edge_image(2, 2);
	ASSERT_NOT_NULL(img);

	ASSERT_EQUAL(IMG2_E_ARG, img2_encoder_dispatch(NULL, IMG2_FORMAT_PNG, 6, &buf, &len));
	ASSERT_EQUAL(IMG2_E_ARG, img2_encoder_dispatch(img, IMG2_FORMAT_NONE, 6, &buf, &len));
	ASSERT_EQUAL(IMG2_E_UNSUPPORTED, img2_encoder_dispatch(img, (img2_format_t)77, 6, &buf, &len));
	img2_encoder_registry_init();
	img2_image_destroy(img);

	/* libjpeg refuses widths above 65500: the encoder's failure is passed through */
	img2_image_t *wide = img2_image_alloc(65501, 1);
	ASSERT_NOT_NULL(wide);
	img2_status_t st = img2_encoder_dispatch(wide, IMG2_FORMAT_JPEG, 90, &buf, &len);
	ASSERT_TRUE(st != IMG2_OK);
	ASSERT_NULL(buf);
	img2_image_destroy(wide);
}

CTEST(core_edges, apply_orientation_from_exif)
{
	ASSERT_EQUAL(IMG2_E_ARG, img2_apply_orientation(NULL));
	img2_image_t *none = NULL;
	ASSERT_EQUAL(IMG2_E_ARG, img2_apply_orientation(&none));

	img2_image_t *img = edge_image(3, 2);
	ASSERT_NOT_NULL(img);
	ASSERT_EQUAL(IMG2_OK, img2_apply_orientation(&img)); /* no EXIF */
	img->exif = calloc(1, sizeof(*img->exif));
	ASSERT_NOT_NULL(img->exif);
	img->exif->orientation = 1;
	ASSERT_EQUAL(IMG2_OK, img2_apply_orientation(&img));
	ASSERT_EQUAL(3, img->width);
	img->exif->orientation = 6;
	ASSERT_EQUAL(IMG2_OK, img2_apply_orientation(&img));
	ASSERT_EQUAL(2, img->width);
	ASSERT_EQUAL(3, img->height);
	img2_image_destroy(img);
}

/* ===== decoder.c ===== */

CTEST(core_edges, decoder_registry_edges)
{
	const uint8_t junk[16] = { 'n', 'o', 't', ' ', 'a', 'n', ' ', 'i', 'm', 'a', 'g', 'e', '!', '!', '!', '!' };
	const uint8_t jpeg[16] = { 0xFF, 0xD8, 0xFF, 0xE0 };

	ASSERT_FALSE(img2_is_animated(NULL, 4));
	ASSERT_FALSE(img2_is_animated(junk, 0));
	ASSERT_FALSE(img2_is_animated(junk, (size_t)IMG2_MAX_FILE_SIZE + 1));
	ASSERT_FALSE(img2_is_animated(junk, sizeof(junk))); /* no decoder */
	ASSERT_FALSE(img2_is_animated(jpeg, sizeof(jpeg))); /* decoder without an animation probe */
	ASSERT_NULL(img2_decoder_info(img2_decoder_count()));
	img2_decoder_registry_init();

	/* frames_adopt: NULL out frees the frames; NULL frames with a success code -> INTERNAL */
	img2_image_t **frames = malloc(2 * sizeof(*frames));
	ASSERT_NOT_NULL(frames);
	frames[0] = img2_image_alloc(1, 1);
	frames[1] = NULL;
	ASSERT_EQUAL(IMG2_E_ARG, img2_frames_adopt(NULL, frames, 2, IMG2_OK));
	img2_frames_t out;
	ASSERT_EQUAL(IMG2_E_INTERNAL, img2_frames_adopt(&out, NULL, 0, IMG2_OK));
	ASSERT_EQUAL(IMG2_E_FORMAT, img2_frames_adopt(&out, NULL, 0, IMG2_E_FORMAT));
	img2_decoder_free_frames(NULL, 3);
}

CTEST(core_edges, dispatch_edges)
{
	img2_frames_t out;
	const uint8_t pgm[] = "P5 2 1 255 \x10\x20";
	ASSERT_EQUAL(IMG2_E_ARG, img2_decoder_dispatch(NULL, pgm, sizeof(pgm) - 1, IMG2_MIME_PNM, NULL));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decoder_dispatch(NULL, NULL, 4, IMG2_MIME_PNM, &out));
	ASSERT_EQUAL(IMG2_E_ARG, img2_decoder_dispatch(NULL, pgm, 0, IMG2_MIME_PNM, &out));
	/* a known but not registered MIME value */
	ASSERT_EQUAL(IMG2_E_UNSUPPORTED, img2_decoder_dispatch(NULL, pgm, sizeof(pgm) - 1, (img2_mime_t)99, &out));

	/* max_frames above the frame count keeps everything */
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	opts.max_frames = 5;
	ASSERT_EQUAL(IMG2_OK, img2_decode(pgm, sizeof(pgm) - 1, &opts, &out));
	ASSERT_EQUAL(1, out.count);
	img2_frames_free(&out);
}

/* ===== log.c ===== */

CTEST(core_edges, log_sanitises_and_handles_errors)
{
	img2_log_level_t saved = img2_get_log_level();
	edge_log_t cap = { 0 };
	img2_set_log_level(IMG2_LOG_DEBUG);
	img2_set_log_callback(edge_log_cb, &cap);

	img2_log(IMG2_LOG_INFO, "a\x01" "b\tc\x7f" "d\ne\n");
	ASSERT_STR("a\\x01b\tc\\x7fd\\x0ae\n", cap.last); /* [F22] lossless escape, final '\n' kept */

	/* vsnprintf failure (unencodable wide character in the C locale) delivers an empty message */
	wchar_t bad[2] = { (wchar_t)0xD800, 0 };
	img2_log(IMG2_LOG_INFO, "%ls", bad);
	ASSERT_STR("", cap.last);

	img2_set_log_callback(NULL, NULL);
	img2_set_log_level(saved);
}

/* ===== magic.c ===== */

static img2_mime_t edge_detect(const char *s, size_t len)
{
	return img2_detect_mime((const uint8_t *)s, len);
}

CTEST(core_edges, magic_signatures)
{
	uint8_t b[64];

	memset(b, 0, sizeof(b));
	memcpy(b, "II*\0", 4);
	ASSERT_EQUAL(IMG2_MIME_TIFF, img2_detect_mime(b, 12));       /* short: no RAW scan */
	ASSERT_EQUAL(IMG2_MIME_TIFF, img2_detect_mime(b, sizeof(b)));
	memcpy(b + 8, "CR\x02\0", 4);
	ASSERT_EQUAL(IMG2_MIME_RAW, img2_detect_mime(b, sizeof(b)));  /* CR2 marker */
	memset(b + 8, 0, 4);
	memcpy(b + 20, "NIKON", 5);
	ASSERT_EQUAL(IMG2_MIME_RAW, img2_detect_mime(b, sizeof(b)));
	memcpy(b, "MM\0*", 4);
	ASSERT_EQUAL(IMG2_MIME_RAW, img2_detect_mime(b, sizeof(b)));
	/* the maker-string scan stops after 2048 bytes */
	uint8_t *big = calloc(1, 4096);
	ASSERT_NOT_NULL(big);
	memcpy(big, "II*\0", 4);
	memcpy(big + 3000, "NIKON", 5);
	ASSERT_EQUAL(IMG2_MIME_TIFF, img2_detect_mime(big, 4096));
	free(big);

	ASSERT_EQUAL(IMG2_MIME_RAW, edge_detect("FUJIFILMCCD-RAW\0" "0201", 20));
	ASSERT_EQUAL(IMG2_MIME_RAW, edge_detect("IIRO\0\0\0\0\0\0\0\0", 12));
	ASSERT_EQUAL(IMG2_MIME_RAW, edge_detect("IIRS\0\0\0\0\0\0\0\0", 12));
	ASSERT_EQUAL(IMG2_MIME_RAW, edge_detect("IIU\0\0\0\0\0\0\0\0\0", 12));
	ASSERT_EQUAL(IMG2_MIME_QOI, edge_detect("qoif\0\0\0\1\0\0\0\1", 12));

	/* ICO / CUR type field, count bounds */
	ASSERT_EQUAL(IMG2_MIME_ICO, edge_detect("\0\0\1\0\1\0\0\0", 8));
	ASSERT_EQUAL(IMG2_MIME_CUR, edge_detect("\0\0\2\0\1\0\0\0", 8));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, edge_detect("\0\0\3\0\1\0\0\0", 8));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, edge_detect("\0\0\1\0\0\0\0\0", 8));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, edge_detect("\0\0\1\0\0\1\0\0", 8));

	/* WebP RIFF without WEBP, HEIF major brands, AVIF sequence, j2k compatible brand */
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, edge_detect("RIFF\0\0\0\0WAVE", 12));
	const char *brands[] = { "heix", "hevc", "hevx", "mif1" };
	for (size_t i = 0; i < 4; i++) {
		char box[16] = { 0, 0, 0, 16, 'f', 't', 'y', 'p' };
		memcpy(box + 8, brands[i], 4);
		ASSERT_EQUAL(IMG2_MIME_HEIF, img2_detect_mime((const uint8_t *)box, sizeof(box)));
	}
	ASSERT_EQUAL(IMG2_MIME_AVIF, edge_detect("\0\0\0\x10" "ftypavis\0\0\0\0", 16));
	ASSERT_EQUAL(IMG2_MIME_HEIF, edge_detect("\0\0\0\x18" "ftypxxxx\0\0\0\0yyyyj2is", 24));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, edge_detect("\0\0\0\x18" "ftypxxxx\0\0\0\0yyyyzzzz", 24));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, edge_detect("\0\0\0\x08" "moovxxxx", 12));

	/* SVG: XML declaration without an svg root; BOM form */
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, edge_detect("<?xml version='1.0'?><html/>", 28));
	ASSERT_EQUAL(IMG2_MIME_SVG, edge_detect("\xEF\xBB\xBF<?xml ?><svg/>", 17));

	/* TGA: each accepted image type and depth; wrong depth */
	for (int t = 0; t < 4; t++) {
		const uint8_t types[4] = { 2, 3, 10, 11 };
		const uint8_t depths[4] = { 8, 16, 24, 32 };
		memset(b, 0, 18);
		b[2] = types[t];
		b[16] = depths[t];
		ASSERT_EQUAL(IMG2_MIME_TGA, img2_detect_mime(b, 18));
		b[16] = 7;
		ASSERT_EQUAL(IMG2_MIME_UNKNOWN, img2_detect_mime(b, 18));
	}

	/* PNM whitespace variants after the magic */
	const char ws[] = { '\t', '\r', '\v', '\f' };
	for (size_t i = 0; i < sizeof(ws); i++) {
		char p[8] = { 'P', '6', ws[i], '1', ' ', '1', ' ', '1' };
		ASSERT_EQUAL(IMG2_MIME_PNM, img2_detect_mime((const uint8_t *)p, sizeof(p)));
	}
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, edge_detect("P8 1 1 1", 8));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, edge_detect("P0 1 1 1", 8));
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, edge_detect("P6x1 1 1", 8));
	ASSERT_EQUAL(IMG2_MIME_HDR, edge_detect("#?RGBE\n\n", 8));
}

CTEST(core_edges, mime_names_and_strings)
{
	for (int m = IMG2_MIME_UNKNOWN; m <= IMG2_MIME_YUV + 1; m++) {
		ASSERT_NOT_NULL(img2_mime_name((img2_mime_t)m));
		ASSERT_NOT_NULL(img2_mime_string((img2_mime_t)m));
	}
	ASSERT_STR("CUR", img2_mime_name(IMG2_MIME_CUR));
	ASSERT_STR("image/x-tga", img2_mime_string(IMG2_MIME_TGA));
	ASSERT_STR("UNKNOWN", img2_mime_name((img2_mime_t)99));
}

/* ===== decoder_stb.c ===== */

CTEST(core_edges, stb_limits_and_arguments)
{
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_stb((const uint8_t *)"BM", 2, NULL, NULL));
	img2_frames_t out;
	ASSERT_EQUAL(IMG2_E_ARG, img2_decode_stb(NULL, 2, NULL, &out));

	/* 24-bit BMP, 65537 x 1: stb_image decodes it, img2 refuses the dimensions */
	const uint32_t w = IMG2_MAX_DIMENSION + 1;
	const uint32_t row = (w * 3 + 3) & ~3u;
	const size_t size = 54 + (size_t)row;
	uint8_t *bmp = calloc(1, size);
	ASSERT_NOT_NULL(bmp);
	bmp[0] = 'B';
	bmp[1] = 'M';
	bmp[2] = (uint8_t)size;
	bmp[3] = (uint8_t)(size >> 8);
	bmp[4] = (uint8_t)(size >> 16);
	bmp[10] = 54;
	bmp[14] = 40;
	bmp[18] = (uint8_t)w;
	bmp[19] = (uint8_t)(w >> 8);
	bmp[20] = (uint8_t)(w >> 16);
	bmp[22] = 1;
	bmp[26] = 1;
	bmp[28] = 24;
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_decode_stb(bmp, size, NULL, &out));
	free(bmp);
}
