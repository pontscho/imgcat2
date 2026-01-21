/**
 * @file test_png_orientation.c
 * @brief Integration test for PNG orientation handling
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

CTEST(png_orientation, apply_orientation_90cw)
{
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	image_set_pixel(img, 0, 0, 255, 0, 0, 255);
	int result = image_apply_orientation(img, 6);
	ASSERT_EQUAL(0, result);
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	image_destroy(img);
}

CTEST(png_orientation, non_square_dimension_swap)
{
	image_t *img = image_create(8, 6);
	ASSERT_NOT_NULL(img);

	int result = image_apply_orientation(img, 6);
	ASSERT_EQUAL(0, result);
	ASSERT_EQUAL(6, img->width);
	ASSERT_EQUAL(8, img->height);

	image_destroy(img);
}
