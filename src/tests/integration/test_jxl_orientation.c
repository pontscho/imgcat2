/**
 * @file test_jxl_orientation.c
 * @brief Integration test for JPEG XL orientation handling
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

CTEST(jxl_orientation, apply_orientation_transpose)
{
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	image_set_pixel(img, 0, 0, 255, 0, 0, 255);

	int result = image_apply_orientation(img, 5);
	ASSERT_EQUAL(0, result);
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	image_destroy(img);
}
