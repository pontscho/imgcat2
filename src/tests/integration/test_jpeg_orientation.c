/**
 * @file test_jpeg_orientation.c
 * @brief Integration test for JPEG orientation handling
 *
 * Tests that JPEG images with EXIF orientation metadata are correctly
 * transformed during the decoding pipeline.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

#ifdef HAVE_EXIF_READER
#include "../../imgcat2/metadata/exif_reader.h"
#endif

/**
 * @test Test that image_apply_orientation handles JPEG format correctly
 *
 * Creates a test JPEG-like image and verifies orientation transformations.
 * This is an integration test verifying the full orientation pipeline.
 */
CTEST(jpeg_orientation, apply_orientation_to_jpeg_image)
{
	/* Create a test image simulating decoded JPEG */
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	/* Set corner colors for verification */
	image_set_pixel(img, 0, 0, 255, 0, 0, 255); /* Top-left: Red */
	image_set_pixel(img, 3, 0, 0, 255, 0, 255); /* Top-right: Green */
	image_set_pixel(img, 0, 3, 0, 0, 255, 255); /* Bottom-left: Blue */
	image_set_pixel(img, 3, 3, 255, 255, 0, 255); /* Bottom-right: Yellow */

#ifdef HAVE_EXIF_READER
	/* Simulate EXIF metadata with orientation=6 (rotate 90° CW) */
	img->exif = calloc(1, sizeof(exif_info_t));
	ASSERT_NOT_NULL(img->exif);
	img->exif->orientation = 6;
#endif

	/* Apply orientation transformation */
	int result = image_apply_orientation(img, 6);
	ASSERT_EQUAL(0, result);

	/* Verify dimensions swapped for 90° rotation: 4x4 → 4x4 (square stays same) */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	/* Verify rotation applied correctly */
	uint8_t *tl = image_get_pixel(img, 0, 0);
	ASSERT_NOT_NULL(tl);
	ASSERT_EQUAL(0, tl[0]); /* Blue */
	ASSERT_EQUAL(0, tl[1]);
	ASSERT_EQUAL(255, tl[2]);

	image_destroy(img);
}

/**
 * @test Test orientation=3 (180° rotation) on JPEG
 */
CTEST(jpeg_orientation, rotate_180_degrees)
{
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	/* Set test pattern */
	image_set_pixel(img, 0, 0, 255, 0, 0, 255); /* Red */
	image_set_pixel(img, 3, 3, 255, 255, 0, 255); /* Yellow */

	/* Apply 180° rotation */
	int result = image_apply_orientation(img, 3);
	ASSERT_EQUAL(0, result);

	/* Verify corners swapped diagonally */
	uint8_t *tl = image_get_pixel(img, 0, 0);
	ASSERT_EQUAL(255, tl[0]); /* Yellow at top-left */
	ASSERT_EQUAL(255, tl[1]);
	ASSERT_EQUAL(0, tl[2]);

	uint8_t *br = image_get_pixel(img, 3, 3);
	ASSERT_EQUAL(255, br[0]); /* Red at bottom-right */
	ASSERT_EQUAL(0, br[1]);
	ASSERT_EQUAL(0, br[2]);

	image_destroy(img);
}

/**
 * @test Test that orientation=1 (normal) leaves JPEG unchanged
 */
CTEST(jpeg_orientation, orientation_normal_no_change)
{
	image_t *img = image_create(4, 4);
	ASSERT_NOT_NULL(img);

	image_set_pixel(img, 0, 0, 255, 0, 0, 255);

	/* Store original */
	uint8_t *orig = image_get_pixel(img, 0, 0);
	uint8_t orig_r = orig[0];

	/* Apply orientation 1 (should do nothing) */
	int result = image_apply_orientation(img, 1);
	ASSERT_EQUAL(0, result);

	/* Verify unchanged */
	uint8_t *after = image_get_pixel(img, 0, 0);
	ASSERT_EQUAL(orig_r, after[0]);

	image_destroy(img);
}

/**
 * @test Test non-square JPEG with dimension swap
 */
CTEST(jpeg_orientation, non_square_dimension_swap)
{
	/* Create 6x4 image */
	image_t *img = image_create(6, 4);
	ASSERT_NOT_NULL(img);

	/* Apply 90° rotation */
	int result = image_apply_orientation(img, 6);
	ASSERT_EQUAL(0, result);

	/* Verify dimensions swapped: 6x4 → 4x6 */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(6, img->height);

	image_destroy(img);
}
