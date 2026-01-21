/**
 * @file test_heif_orientation.c
 * @brief Integration test for HEIF orientation handling
 *
 * IMPORTANT: libheif handles EXIF orientation transformations natively during decode.
 * The pipeline should NOT apply additional transformations to HEIF/AVIF images.
 * These tests verify the orientation function works, but in practice it's not called for HEIF.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

CTEST(heif_orientation, libheif_handles_orientation_natively)
{
	/* libheif automatically applies EXIF orientation during decode */
	/* Pipeline excludes HEIF from image_apply_orientation() calls */
	/* This test documents the expected behavior */

	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	/* Simulate already-rotated HEIF image from libheif */
	/* No additional transformation should be applied by pipeline */
	int result = image_apply_orientation(img, 1);
	ASSERT_EQUAL(0, result);

	image_destroy(img);
}

CTEST(heif_orientation, function_works_if_manually_called)
{
	/* Even though pipeline doesn't call this for HEIF, */
	/* the function should still work correctly if manually invoked */

	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	image_set_pixel(img, 0, 0, 255, 0, 0, 255);
	image_set_pixel(img, 3, 3, 0, 255, 0, 255);

	int result = image_apply_orientation(img, 3);
	ASSERT_EQUAL(0, result);

	uint8_t *tl = image_get_pixel(img, 0, 0);
	ASSERT_EQUAL(0, tl[0]);
	ASSERT_EQUAL(255, tl[1]);

	image_destroy(img);
}
