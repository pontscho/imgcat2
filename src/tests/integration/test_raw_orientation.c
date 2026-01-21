/**
 * @file test_raw_orientation.c
 * @brief Integration test for RAW format orientation handling
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

CTEST(raw_orientation, apply_orientation_transverse)
{
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	image_set_pixel(img, 0, 0, 255, 0, 0, 255);

	int result = image_apply_orientation(img, 7);
	ASSERT_EQUAL(0, result);
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	image_destroy(img);
}
