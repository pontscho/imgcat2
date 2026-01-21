/**
 * @file test_webp_orientation.c
 * @brief Integration test for WebP orientation handling
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

CTEST(webp_orientation, apply_orientation_flip_horizontal)
{
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	image_set_pixel(img, 0, 0, 255, 0, 0, 255);
	image_set_pixel(img, 3, 0, 0, 255, 0, 255);

	int result = image_apply_orientation(img, 2);
	ASSERT_EQUAL(0, result);

	uint8_t *tl = image_get_pixel(img, 0, 0);
	ASSERT_EQUAL(0, tl[0]);
	ASSERT_EQUAL(255, tl[1]);

	image_destroy(img);
}

CTEST(webp_orientation, rotate_270cw)
{
	image_t *img = image_create(6, 4);
	ASSERT_NOT_NULL(img);

	int result = image_apply_orientation(img, 8);
	ASSERT_EQUAL(0, result);
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(6, img->height);

	image_destroy(img);
}
