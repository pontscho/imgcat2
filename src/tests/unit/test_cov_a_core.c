/**
 * @file test_cov_a_core.c
 * @brief Remaining core edge cases: image.c argument paths, MIME detection fall-through
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <img2/img2.h>

#include "../../libimg2/core/image.h"
#include "../ctest.h"

CTEST(core_image, destroy_without_pixels)
{
	img2_image_t *img = img2_image_alloc(2, 2);
	ASSERT_NOT_NULL(img);
	free(img->pixels);
	img->pixels = NULL;
	img2_image_destroy(img); /* frees only the struct */
}

CTEST(core_image, fit_size_needs_both_outputs)
{
	uint32_t w = 0;
	ASSERT_EQUAL(IMG2_E_ARG, img2_fit_size(10, 10, 5, 5, &w, NULL));
}

CTEST(core_image, internal_scalers_reject_missing_pixels)
{
	img2_image_t *img = img2_image_alloc(4, 4);
	ASSERT_NOT_NULL(img);
	uint8_t *px = img->pixels;
	img->pixels = NULL;
	img2_image_t *out = NULL;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_resize(img, 2, 2, &out));
	ASSERT_NULL(out);
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_fit(img, 2, 2, &out));
	ASSERT_NULL(out);
	img->pixels = px;
	img2_image_destroy(img);
}

CTEST(core_magic, zero_byte_then_nonzero_is_unknown)
{
	/* 0x00 followed by a non-zero byte: not ICO/CUR, nothing else matches */
	const uint8_t data[8] = { 0x00, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00 };
	ASSERT_EQUAL(IMG2_MIME_UNKNOWN, img2_detect_mime(data, sizeof(data)));
}
