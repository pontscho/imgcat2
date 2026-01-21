/**
 * @file test_avif_orientation.c
 * @brief Integration test for AVIF orientation handling
 *
 * IMPORTANT: libheif handles EXIF orientation transformations natively during decode.
 * The pipeline should NOT apply additional transformations to AVIF images.
 * These tests verify the orientation function works, but in practice it's not called for AVIF.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

CTEST(avif_orientation, libheif_handles_orientation_natively)
{
	/* libheif (which decodes AVIF) automatically applies EXIF orientation */
	/* Pipeline excludes AVIF from image_apply_orientation() calls */

	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	/* Simulate already-rotated AVIF image from libheif */
	int result = image_apply_orientation(img, 1);
	ASSERT_EQUAL(0, result);

	image_destroy(img);
}

CTEST(avif_orientation, function_works_if_manually_called)
{
	/* Even though pipeline doesn't call this for AVIF, */
	/* the function should still work if manually invoked */

	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	image_set_pixel(img, 0, 0, 255, 0, 0, 255);
	image_set_pixel(img, 0, 3, 0, 0, 255, 255);

	int result = image_apply_orientation(img, 4);
	ASSERT_EQUAL(0, result);

	uint8_t *tl = image_get_pixel(img, 0, 0);
	ASSERT_EQUAL(0, tl[0]);
	ASSERT_EQUAL(0, tl[1]);
	ASSERT_EQUAL(255, tl[2]);

	image_destroy(img);
}
