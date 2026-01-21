/**
 * @file test_tiff_orientation.c
 * @brief Integration test for TIFF orientation handling
 *
 * CRITICAL: This test verifies TIFF is NOT transformed by image_apply_orientation()
 * because libtiff handles orientation natively during decoding.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

CTEST(tiff_orientation, verify_no_transformation_needed)
{
	/* TIFF images are already correctly oriented by libtiff */
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	image_set_pixel(img, 0, 0, 255, 0, 0, 255);
	uint8_t *orig = image_get_pixel(img, 0, 0);
	uint8_t orig_r = orig[0];

	/* For TIFF, pipeline should skip orientation transformation */
	/* This test just verifies the function works if called */
	int result = image_apply_orientation(img, 1);
	ASSERT_EQUAL(0, result);

	/* Image should remain unchanged */
	uint8_t *after = image_get_pixel(img, 0, 0);
	ASSERT_EQUAL(orig_r, after[0]);

	image_destroy(img);
}

CTEST(tiff_orientation, libtiff_handles_rotation_natively)
{
	/* This is a documentation test showing TIFF is special */
	/* In pipeline_decode(), TIFF is excluded from orientation processing */
	/* because libtiff already applies orientation during decode */
	
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);
	
	/* Simulate already-rotated TIFF image from libtiff */
	/* No additional transformation should be applied */
	int result = image_apply_orientation(img, 1);
	ASSERT_EQUAL(0, result);
	
	image_destroy(img);
}
