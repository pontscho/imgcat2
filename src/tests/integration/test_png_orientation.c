/**
 * @file test_png_orientation.c
 * @brief Integration test for PNG orientation handling
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../libimg2/core/image.h"
#include "../ctest.h"

CTEST(png_orientation, apply_orientation_90cw)
{
	img2_image_t *img = img2_image_alloc(4, 4);
	ASSERT_NOT_NULL(img);

	img2_image_set_pixel(img, 0, 0, 255, 0, 0, 255);
	int result = img2_image_orient(img, 6);
	ASSERT_EQUAL(0, result);
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	img2_image_destroy(img);
}

CTEST(png_orientation, non_square_dimension_swap)
{
	img2_image_t *img = img2_image_alloc(8, 6);
	ASSERT_NOT_NULL(img);

	int result = img2_image_orient(img, 6);
	ASSERT_EQUAL(0, result);
	ASSERT_EQUAL(6, img->width);
	ASSERT_EQUAL(8, img->height);

	img2_image_destroy(img);
}
