/**
 * @file test_sanitize_regress.c
 * @brief Regression tests of the findings of the sanitizer sweep (docs/sanitizers.md)
 *
 * raw_iso_above_65535_exact: LibRaw reports the ISO as a float; decoder_raw.c converted it with a plain
 * (uint16_t) cast, which is undefined for values above 65535 (UBSan float-cast-overflow) and on x86 wrapped
 * ISO 102400 of the Nikon Z 9 fixture to 36864. The fix first saturated at 65535; iso_speed is a uint32_t now
 * and the real ISO 102400 is reported.
 *
 * svg_size_beyond_uint32_is_limit: decoder_nanosvg.c converted roundf(width / height) to uint32_t before the
 * dimension check; for sizes >= 2^32 (or infinite) that is undefined, and on x86 a width of 4294967808 (2^32 + 512)
 * became 512 and passed the IMG2_MAX_DIMENSION check, 1e30 became 0 (IMG2_E_FORMAT instead of IMG2_E_LIMIT).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/decoders/decoder.h"
#include "../../libimg2/metadata/exif_reader.h"
#include "../ctest.h"

#ifndef IMG2_TEST_SRC_DIR
#error "IMG2_TEST_SRC_DIR must be defined by the build system"
#endif

static uint8_t *load_fixture(const char *rel, size_t *len)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s/data/%s", IMG2_TEST_SRC_DIR, rel);
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return NULL;
	}
	uint8_t *buf = NULL;
	if (fseek(f, 0, SEEK_END) == 0) {
		long n = ftell(f);
		if (n > 0 && fseek(f, 0, SEEK_SET) == 0) {
			buf = malloc((size_t)n);
			if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
				free(buf);
				buf = NULL;
			} else if (buf != NULL) {
				*len = (size_t)n;
			}
		}
	}
	fclose(f);
	return buf;
}

CTEST(sanitize_regress, raw_iso_above_65535_exact)
{
	/* Nikon Z 9 at ISO 102400 (LibRaw other.iso_speed = 102400.0f) */
	size_t len = 0;
	uint8_t *data = load_fixture("raw/nikon_z_9_high_efficiency_compressed_dx_cropped_max_overexposed.dng", &len);
	ASSERT_NOT_NULL(data);
	img2_frames_t fr = { 0 };
	ASSERT_EQUAL(IMG2_OK, img2_decode_raw(data, len, NULL, &fr));
	ASSERT_EQUAL(1, fr.count);
	const img2_exif_t *e = fr.frames[0]->exif;
	ASSERT_NOT_NULL(e);
	ASSERT_TRUE(e->has_iso);
	ASSERT_EQUAL(102400, e->iso_speed);
	img2_frames_free(&fr);
	free(data);
}

static img2_status_t svg_decode_str(const char *svg)
{
	img2_frames_t fr = { 0 };
	img2_status_t st = img2_decode_svg_nanosvg((const uint8_t *)svg, strlen(svg), NULL, &fr);
	img2_frames_free(&fr);
	return st;
}

CTEST(sanitize_regress, svg_size_beyond_uint32_is_limit)
{
	static const char *const svgs[] = {
		"<svg width=\"4294967808\" height=\"10\"></svg>",
		"<svg width=\"10\" height=\"4294967808\"></svg>",
		"<svg width=\"1e30\" height=\"10\"></svg>",
		"<svg width=\"10\" height=\"1e30\"></svg>",
		"<svg width=\"1e39\" height=\"1e39\"></svg>", /* float overflow: infinite */
	};
	for (size_t i = 0; i < sizeof(svgs) / sizeof(svgs[0]); i++) {
		ASSERT_EQUAL(IMG2_E_LIMIT, svg_decode_str(svgs[i]));
	}
	/* the largest size that still converts: rejected by the dimension check as before */
	ASSERT_EQUAL(IMG2_E_LIMIT, svg_decode_str("<svg width=\"4294967040\" height=\"10\"></svg>"));
}
