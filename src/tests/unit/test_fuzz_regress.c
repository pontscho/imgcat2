/**
 * @file test_fuzz_regress.c
 * @brief Regression tests of the libFuzzer findings (docs/fuzzing.md): each input is the minimised or the
 *        hand-reduced reproducer of one finding, embedded as a C array, and the test checks the fixed behaviour
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../ctest.h"

#ifdef IMG2_HAVE_HEIF
#include "cov_c_heif_util.h"
#endif

/* Decode a copy of buf (exactly len bytes on the heap, so an over-read is a real out-of-bounds access) */
static img2_status_t decode_copy(const uint8_t *buf, size_t len, size_t pad_zeros, img2_mime_t force, img2_frames_t *out)
{
	uint8_t *copy = calloc(1, len + pad_zeros);
	if (copy == NULL) {
		return IMG2_E_NOMEM;
	}
	memcpy(copy, buf, len);
	img2_decode_opts_t opts = IMG2_DECODE_OPTS_INIT;
	opts.force_mime = force;
	img2_status_t st = img2_decode(copy, len + pad_zeros, &opts, out);
	free(copy);
	return st;
}

/*
 * Sanitizer findings without an observable effect in a plain build (undefined behaviour that x86 happens to
 * compute "harmlessly") fail these tests only in the sanitized IMG2_FUZZ build (build-fuzz/, IMG2_BUILD_TESTS=ON,
 * UBSan aborts): there they are the red/green evidence; elsewhere they pin the decode result.
 */

/*
 * fuzz_decode crash-4d517aea (minimised): nanosvgrast.h nsvg__addActive converted an edge's fixed-point x / dx
 * (8222224 * 1024 = 8.4e9) to int: float-cast-overflow, and the per-scanline x += dx could overflow int too.
 */
static const char k_svg_edge_overflow[] = "<svg width='9><polygon points='0,0 8222224,8'><";

CTEST(fuzz_regress, svg_edge_x_saturates)
{
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, decode_copy((const uint8_t *)k_svg_edge_overflow, sizeof(k_svg_edge_overflow) - 1, 0, IMG2_MIME_UNKNOWN, &fr));
	ASSERT_EQUAL(1, fr.count);
	img2_frames_free(&fr);
}

/*
 * fuzz_decode_forced crash-1fd75303 (SVG, decoder_nanosvg.c:276 float-cast-overflow): the parsed SVG width / height
 * (float) went through (uint32_t)roundf() before the dimension check. 4294967808 = 2^32 + 512 is a float; a GCC build
 * truncated it to 512 and rendered a 512 x 4 image instead of rejecting it.
 */
CTEST(fuzz_regress, svg_huge_width_is_limit)
{
	static const char svg[] = "<svg xmlns='http://www.w3.org/2000/svg' width='4294967808' height='4'></svg>";
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_LIMIT, decode_copy((const uint8_t *)svg, sizeof(svg) - 1, 0, IMG2_MIME_UNKNOWN, &fr));
}

CTEST(fuzz_regress, svg_huge_height_is_limit)
{
	static const char svg[] = "<svg xmlns='http://www.w3.org/2000/svg' width='4' height='1e30'></svg>";
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_E_LIMIT, decode_copy((const uint8_t *)svg, sizeof(svg) - 1, 0, IMG2_MIME_UNKNOWN, &fr));
}

/*
 * Found while triaging the fuzzer's text-heavy SVG inputs (slow-unit-85b5c61e, timeout-00af8603): nanosvg.h's
 * nsvg__decodeUTF8 advanced by the full sequence length of a lead byte even when the text ended inside the sequence,
 * stepping over the NUL and rendering stale bytes left in the parser's text buffer by an earlier, longer <text>.
 * A text ending in a cut-short sequence must render like '?' (the replacement nanosvg uses for invalid bytes).
 */
static img2_image_t *svg_render(const char *svg)
{
	img2_frames_t fr;
	if (decode_copy((const uint8_t *)svg, strlen(svg), 0, IMG2_MIME_UNKNOWN, &fr) != IMG2_OK) {
		return NULL;
	}
	img2_image_t *img = fr.frames[0];
	fr.frames[0] = NULL;
	img2_frames_free(&fr);
	return img;
}

CTEST(fuzz_regress, svg_text_truncated_utf8_stays_in_string)
{
	static const char *const lead[] = { "\xc3", "\xe2\x82", "\xf0\x9f\x98" }; /* 2-, 3-, 4-byte sequences cut short */
	for (size_t i = 0; i < sizeof(lead) / sizeof(lead[0]); i++) {
		char cut[256];
		snprintf(cut, sizeof(cut), "<svg width='160' height='40'><text x='2' y='14'>WWWWWWWWWW</text><text x='2' y='34'>%s</text></svg>", lead[i]);
		img2_image_t *a = svg_render(cut);
		img2_image_t *b = svg_render("<svg width='160' height='40'><text x='2' y='14'>WWWWWWWWWW</text><text x='2' y='34'>?</text></svg>");
		ASSERT_NOT_NULL(a);
		ASSERT_NOT_NULL(b);
		ASSERT_EQUAL(0, memcmp(a->pixels, b->pixels, (size_t)a->stride * a->height));
		img2_image_destroy(a);
		img2_image_destroy(b);
	}
}

#if defined(IMG2_HAVE_HEIF) && defined(IMG2_HAVE_EXIF_READER)
/*
 * fuzz_decode crash-d2efec66 (29 KB HEIF, mutated src/tests/data/heif/samplefilehub.heif): the HEIF and AVIF EXIF
 * readers built the 4-byte big-endian TIFF offset as (exif_data[0] << 24) | ... on int: a first byte >= 0x80 is a
 * signed shift overflow (UBSan shift-base). Rebuilt with libheif: one 8x8 image (uncompressed codec -> HEIF, AV1
 * -> AVIF) carrying an Exif item whose offset field is ff 00 00 00 (larger than the item: the data is parsed as is,
 * is not TIFF, and the image keeps no EXIF).
 */
static const uint8_t k_exif_item_high_offset[] = { 0xff, 0x00, 0x00, 0x00, 'E', 'x', 'i', 'f', 0x00, 0x00, 'I', 'I', 0x2a, 0x00 };

static void exif_offset_case(enum heif_compression_format comp, img2_mime_t want)
{
	hb_meta_t meta = { "Exif", NULL, k_exif_item_high_offset, (int)sizeof(k_exif_item_high_offset) };
	size_t len = 0;
	uint8_t *file = hb_list(comp, 1, 8, 8, &meta, 1, &len);
	ASSERT_NOT_NULL(file);
	ASSERT_EQUAL(want, img2_detect_mime(file, len));
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, decode_copy(file, len, 0, IMG2_MIME_UNKNOWN, &fr));
	free(file);
	ASSERT_EQUAL(1, fr.count);
	ASSERT_NULL(fr.frames[0]->exif);
	img2_frames_free(&fr);
}

CTEST(fuzz_regress, heif_exif_offset_high_byte)
{
	exif_offset_case(heif_compression_uncompressed, IMG2_MIME_HEIF);
}

CTEST(fuzz_regress, avif_exif_offset_high_byte)
{
	exif_offset_case(heif_compression_AV1, IMG2_MIME_AVIF);
}
#endif

#if defined(IMG2_HAVE_RAW) && defined(IMG2_HAVE_EXIF_READER)
/*
 * fuzz_decode (seed src/tests/data/raw/nikon_z_9_..._max_overexposed.dng, ISO 102400): decoder_raw.c converted
 * LibRaw's float iso_speed to uint16_t without a range check (UBSan float-cast-overflow; GCC builds stored
 * 102400 mod 65536 = 36864). Reduced: a 24x24 CFA DNG, Make SONY, ISOSpeedRatings 65535 and
 * RecommendedExposureIndex 102400 (LibRaw takes the latter for SONY), the 1152-byte zero raw strip appended.
 * iso_speed is a uint32_t now: the real 102400 is kept; a RecommendedExposureIndex of 0xFFFFFFFF (a float of 2^32)
 * saturates at UINT32_MAX instead of an undefined conversion.
 */
#define K_RAW_ISO_REI_OFFSET 30 /* the RecommendedExposureIndex value (LONG, inline, little-endian) */
static const uint8_t k_raw_iso_102400[] = {
	0x49, 0x49, 0x2a, 0x00, 0x9e, 0x00, 0x00, 0x00, 0x02, 0x00, 0x27, 0x88, 0x03, 0x00, 0x01, 0x00,
	0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x32, 0x88, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x90,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x53, 0x4f, 0x4e, 0x59, 0x00, 0x00, 0x49, 0x4c, 0x43, 0x45,
	0x2d, 0x31, 0x00, 0x00, 0x49, 0x6d, 0x67, 0x32, 0x20, 0x49, 0x53, 0x4f, 0x00, 0x00, 0x0a, 0x00,
	0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0xfd, 0xff, 0xff, 0xff, 0x0a, 0x00, 0x00, 0x00, 0xff, 0xff,
	0xff, 0xff, 0x0a, 0x00, 0x00, 0x00, 0xfc, 0xff, 0xff, 0xff, 0x0a, 0x00, 0x00, 0x00, 0x0c, 0x00,
	0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0xff, 0xff,
	0xff, 0xff, 0x0a, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x06, 0x00,
	0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x15, 0x00,
	0xfe, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x04, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x01, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x18, 0x00, 0x00, 0x00, 0x02, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
	0x03, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x23, 0x80, 0x00, 0x00, 0x0f, 0x01, 0x02, 0x00, 0x05, 0x00, 0x00, 0x00,
	0x26, 0x00, 0x00, 0x00, 0x10, 0x01, 0x02, 0x00, 0x07, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00,
	0x11, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0xa0, 0x01, 0x00, 0x00, 0x15, 0x01, 0x03, 0x00,
	0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x16, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x18, 0x00, 0x00, 0x00, 0x17, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x80, 0x04, 0x00, 0x00,
	0x1c, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x8d, 0x82, 0x03, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x02, 0x00, 0x8e, 0x82, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00,
	0x00, 0x01, 0x01, 0x02, 0x69, 0x87, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
	0x12, 0xc6, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x01, 0x04, 0x00, 0x00, 0x14, 0xc6, 0x02, 0x00,
	0x09, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x1d, 0xc6, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00,
	0xff, 0xff, 0x00, 0x00, 0x21, 0xc6, 0x0a, 0x00, 0x09, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00,
	0x28, 0xc6, 0x05, 0x00, 0x03, 0x00, 0x00, 0x00, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

CTEST(fuzz_regress, raw_iso_above_uint16_kept)
{
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, decode_copy(k_raw_iso_102400, sizeof(k_raw_iso_102400), 24 * 24 * 2, IMG2_MIME_UNKNOWN, &fr));
	ASSERT_EQUAL(1, fr.count);
	const img2_exif_t *e = fr.frames[0]->exif;
	ASSERT_NOT_NULL(e);
	ASSERT_TRUE(e->has_iso);
	ASSERT_EQUAL(102400, e->iso_speed);
	img2_frames_free(&fr);
}

CTEST(fuzz_regress, raw_iso_above_uint32_saturates)
{
	uint8_t buf[sizeof(k_raw_iso_102400)];
	memcpy(buf, k_raw_iso_102400, sizeof(buf));
	ASSERT_EQUAL(0x00, buf[K_RAW_ISO_REI_OFFSET]); /* 102400 = 0x00019000 */
	ASSERT_EQUAL(0x90, buf[K_RAW_ISO_REI_OFFSET + 1]);
	memset(buf + K_RAW_ISO_REI_OFFSET, 0xff, 4);
	img2_frames_t fr;
	ASSERT_EQUAL(IMG2_OK, decode_copy(buf, sizeof(buf), 24 * 24 * 2, IMG2_MIME_UNKNOWN, &fr));
	ASSERT_EQUAL(1, fr.count);
	const img2_exif_t *e = fr.frames[0]->exif;
	ASSERT_NOT_NULL(e);
	ASSERT_TRUE(e->has_iso);
	ASSERT_EQUAL(UINT32_MAX, e->iso_speed);
	img2_frames_free(&fr);
}
#endif
