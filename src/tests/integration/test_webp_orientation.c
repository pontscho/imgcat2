/**
 * @file test_webp_orientation.c
 * @brief Integration test for WebP orientation handling
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../libimg2/core/image.h"
#include "../ctest.h"

CTEST(webp_orientation, apply_orientation_flip_horizontal)
{
	img2_image_t *img = img2_image_alloc(4, 4);
	ASSERT_NOT_NULL(img);

	img2_image_set_pixel(img, 0, 0, 255, 0, 0, 255);
	img2_image_set_pixel(img, 3, 0, 0, 255, 0, 255);

	int result = img2_image_orient(img, 2);
	ASSERT_EQUAL(0, result);

	uint8_t *tl = img2_image_get_pixel(img, 0, 0);
	ASSERT_EQUAL(0, tl[0]);
	ASSERT_EQUAL(255, tl[1]);

	img2_image_destroy(img);
}

CTEST(webp_orientation, rotate_270cw)
{
	img2_image_t *img = img2_image_alloc(6, 4);
	ASSERT_NOT_NULL(img);

	int result = img2_image_orient(img, 8);
	ASSERT_EQUAL(0, result);
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(6, img->height);

	img2_image_destroy(img);
}
