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

#include "../../libimg2/core/image.h"
#include "../ctest.h"

#ifdef IMG2_HAVE_EXIF_READER
#include "../../libimg2/metadata/exif_reader.h"
#endif

/**
 * @test Test that img2_image_orient handles JPEG format correctly
 *
 * Creates a test JPEG-like image and verifies orientation transformations.
 * This is an integration test verifying the full orientation pipeline.
 */
CTEST(jpeg_orientation, apply_orientation_to_jpeg_image)
{
	/* Create a test image simulating decoded JPEG */
	img2_image_t *img = img2_image_alloc(4, 4);
	ASSERT_NOT_NULL(img);

	/* Set corner colors for verification */
	img2_image_set_pixel(img, 0, 0, 255, 0, 0, 255); /* Top-left: Red */
	img2_image_set_pixel(img, 3, 0, 0, 255, 0, 255); /* Top-right: Green */
	img2_image_set_pixel(img, 0, 3, 0, 0, 255, 255); /* Bottom-left: Blue */
	img2_image_set_pixel(img, 3, 3, 255, 255, 0, 255); /* Bottom-right: Yellow */

#ifdef IMG2_HAVE_EXIF_READER
	/* Simulate EXIF metadata with orientation=6 (rotate 90° CW) */
	img->exif = calloc(1, sizeof(img2_exif_t));
	ASSERT_NOT_NULL(img->exif);
	img->exif->orientation = 6;
#endif

	/* Apply orientation transformation */
	int result = img2_image_orient(img, 6);
	ASSERT_EQUAL(0, result);

	/* Verify dimensions swapped for 90° rotation: 4x4 → 4x4 (square stays same) */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	/* Verify rotation applied correctly */
	uint8_t *tl = img2_image_get_pixel(img, 0, 0);
	ASSERT_NOT_NULL(tl);
	ASSERT_EQUAL(0, tl[0]); /* Blue */
	ASSERT_EQUAL(0, tl[1]);
	ASSERT_EQUAL(255, tl[2]);

	img2_image_destroy(img);
}

/**
 * @test Test orientation=3 (180° rotation) on JPEG
 */
CTEST(jpeg_orientation, rotate_180_degrees)
{
	img2_image_t *img = img2_image_alloc(4, 4);
	ASSERT_NOT_NULL(img);

	/* Set test pattern */
	img2_image_set_pixel(img, 0, 0, 255, 0, 0, 255); /* Red */
	img2_image_set_pixel(img, 3, 3, 255, 255, 0, 255); /* Yellow */

	/* Apply 180° rotation */
	int result = img2_image_orient(img, 3);
	ASSERT_EQUAL(0, result);

	/* Verify corners swapped diagonally */
	uint8_t *tl = img2_image_get_pixel(img, 0, 0);
	ASSERT_EQUAL(255, tl[0]); /* Yellow at top-left */
	ASSERT_EQUAL(255, tl[1]);
	ASSERT_EQUAL(0, tl[2]);

	uint8_t *br = img2_image_get_pixel(img, 3, 3);
	ASSERT_EQUAL(255, br[0]); /* Red at bottom-right */
	ASSERT_EQUAL(0, br[1]);
	ASSERT_EQUAL(0, br[2]);

	img2_image_destroy(img);
}

/**
 * @test Test that orientation=1 (normal) leaves JPEG unchanged
 */
CTEST(jpeg_orientation, orientation_normal_no_change)
{
	img2_image_t *img = img2_image_alloc(4, 4);
	ASSERT_NOT_NULL(img);

	img2_image_set_pixel(img, 0, 0, 255, 0, 0, 255);

	/* Store original */
	uint8_t *orig = img2_image_get_pixel(img, 0, 0);
	uint8_t orig_r = orig[0];

	/* Apply orientation 1 (should do nothing) */
	int result = img2_image_orient(img, 1);
	ASSERT_EQUAL(0, result);

	/* Verify unchanged */
	uint8_t *after = img2_image_get_pixel(img, 0, 0);
	ASSERT_EQUAL(orig_r, after[0]);

	img2_image_destroy(img);
}

/**
 * @test Test non-square JPEG with dimension swap
 */
CTEST(jpeg_orientation, non_square_dimension_swap)
{
	/* Create 6x4 image */
	img2_image_t *img = img2_image_alloc(6, 4);
	ASSERT_NOT_NULL(img);

	/* Apply 90° rotation */
	int result = img2_image_orient(img, 6);
	ASSERT_EQUAL(0, result);

	/* Verify dimensions swapped: 6x4 → 4x6 */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(6, img->height);

	img2_image_destroy(img);
}
