/**
 * @file test_jxl_orientation.c
 * @brief Integration test for JPEG XL orientation handling
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../libimg2/core/image.h"
#include "../ctest.h"

CTEST(jxl_orientation, apply_orientation_transpose)
{
	img2_image_t *img = img2_image_alloc(4, 4);
	ASSERT_NOT_NULL(img);

	img2_image_set_pixel(img, 0, 0, 255, 0, 0, 255);

	int result = img2_image_orient(img, 5);
	ASSERT_EQUAL(0, result);
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	img2_image_destroy(img);
}
