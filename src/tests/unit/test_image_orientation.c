/**
 * @file test_image_orientation.c
 * @brief Unit tests for image orientation transformation
 *
 * Tests image_apply_orientation() for all 8 EXIF orientation values
 * and verifies correct transformation of pixel data.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

/**
 * @brief Create test pattern image with unique corner colors
 *
 * Creates a 4x4 test image with distinct colors at each corner:
 * - Top-left: Red (255,0,0,255)
 * - Top-right: Green (0,255,0,255)
 * - Bottom-left: Blue (0,0,255,255)
 * - Bottom-right: Yellow (255,255,0,255)
 * - Center pixels: Gray (128,128,128,255)
 *
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @return New test pattern image, or NULL on error
 */
static image_t *create_test_pattern(uint32_t width, uint32_t height)
{
	image_t *img = image_create(width, height);
	if (img == NULL) {
		return NULL;
	}

	/* Fill with gray center */
	for (uint32_t y = 0; y < height; y++) {
		for (uint32_t x = 0; x < width; x++) {
			image_set_pixel(img, x, y, 128, 128, 128, 255);
		}
	}

	/* Set corner colors for easy verification */
	image_set_pixel(img, 0, 0, 255, 0, 0, 255); /* Top-left: Red */
	image_set_pixel(img, width - 1, 0, 0, 255, 0, 255); /* Top-right: Green */
	image_set_pixel(img, 0, height - 1, 0, 0, 255, 255); /* Bottom-left: Blue */
	image_set_pixel(img, width - 1, height - 1, 255, 255, 0, 255); /* Bottom-right: Yellow */

	return img;
}

/**
 * @brief Helper to check if pixel matches expected RGBA values
 */
static bool check_pixel(const image_t *img, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	uint8_t *pixel = image_get_pixel(img, x, y);
	if (pixel == NULL) {
		return false;
	}
	return (pixel[0] == r && pixel[1] == g && pixel[2] == b && pixel[3] == a);
}

/**
 * @test Test orientation=1 (normal, no transformation)
 *
 * Verifies that orientation value 1 returns immediately without changes.
 */
CTEST(image_orientation, orientation_1_normal)
{
	image_t *img = create_test_pattern(4, 4);
	ASSERT_NOT_NULL(img);

	/* Store original corner values */
	uint8_t *tl = image_get_pixel(img, 0, 0);
	uint8_t orig_tl[4] = { tl[0], tl[1], tl[2], tl[3] };

	/* Apply orientation 1 (should do nothing) */
	int result = image_apply_orientation(img, 1);
	ASSERT_EQUAL(0, result);

	/* Verify dimensions unchanged */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	/* Verify pixels unchanged */
	ASSERT_TRUE(check_pixel(img, 0, 0, orig_tl[0], orig_tl[1], orig_tl[2], orig_tl[3]));
	ASSERT_TRUE(check_pixel(img, 0, 0, 255, 0, 0, 255)); /* Top-left: Red */
	ASSERT_TRUE(check_pixel(img, 3, 0, 0, 255, 0, 255)); /* Top-right: Green */
	ASSERT_TRUE(check_pixel(img, 0, 3, 0, 0, 255, 255)); /* Bottom-left: Blue */
	ASSERT_TRUE(check_pixel(img, 3, 3, 255, 255, 0, 255)); /* Bottom-right: Yellow */

	image_destroy(img);
}

/**
 * @test Test orientation=2 (flip horizontal)
 *
 * Verifies horizontal flip transformation.
 */
CTEST(image_orientation, orientation_2_flip_horizontal)
{
	image_t *img = create_test_pattern(4, 4);
	ASSERT_NOT_NULL(img);

	/* Apply orientation 2 (flip horizontal) */
	int result = image_apply_orientation(img, 2);
	ASSERT_EQUAL(0, result);

	/* Verify dimensions unchanged */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	/* Verify corners swapped horizontally */
	ASSERT_TRUE(check_pixel(img, 0, 0, 0, 255, 0, 255)); /* Top-left now has Green (was top-right) */
	ASSERT_TRUE(check_pixel(img, 3, 0, 255, 0, 0, 255)); /* Top-right now has Red (was top-left) */
	ASSERT_TRUE(check_pixel(img, 0, 3, 255, 255, 0, 255)); /* Bottom-left now has Yellow (was bottom-right) */
	ASSERT_TRUE(check_pixel(img, 3, 3, 0, 0, 255, 255)); /* Bottom-right now has Blue (was bottom-left) */

	image_destroy(img);
}

/**
 * @test Test orientation=3 (rotate 180°)
 *
 * Verifies 180 degree rotation.
 */
CTEST(image_orientation, orientation_3_rotate_180)
{
	image_t *img = create_test_pattern(4, 4);
	ASSERT_NOT_NULL(img);

	/* Apply orientation 3 (rotate 180°) */
	int result = image_apply_orientation(img, 3);
	ASSERT_EQUAL(0, result);

	/* Verify dimensions unchanged */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	/* Verify corners rotated 180° */
	ASSERT_TRUE(check_pixel(img, 0, 0, 255, 255, 0, 255)); /* Top-left now has Yellow (was bottom-right) */
	ASSERT_TRUE(check_pixel(img, 3, 0, 0, 0, 255, 255)); /* Top-right now has Blue (was bottom-left) */
	ASSERT_TRUE(check_pixel(img, 0, 3, 0, 255, 0, 255)); /* Bottom-left now has Green (was top-right) */
	ASSERT_TRUE(check_pixel(img, 3, 3, 255, 0, 0, 255)); /* Bottom-right now has Red (was top-left) */

	image_destroy(img);
}

/**
 * @test Test orientation=4 (flip vertical)
 *
 * Verifies vertical flip transformation.
 */
CTEST(image_orientation, orientation_4_flip_vertical)
{
	image_t *img = create_test_pattern(4, 4);
	ASSERT_NOT_NULL(img);

	/* Apply orientation 4 (flip vertical) */
	int result = image_apply_orientation(img, 4);
	ASSERT_EQUAL(0, result);

	/* Verify dimensions unchanged */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	/* Verify corners swapped vertically */
	ASSERT_TRUE(check_pixel(img, 0, 0, 0, 0, 255, 255)); /* Top-left now has Blue (was bottom-left) */
	ASSERT_TRUE(check_pixel(img, 3, 0, 255, 255, 0, 255)); /* Top-right now has Yellow (was bottom-right) */
	ASSERT_TRUE(check_pixel(img, 0, 3, 255, 0, 0, 255)); /* Bottom-left now has Red (was top-left) */
	ASSERT_TRUE(check_pixel(img, 3, 3, 0, 255, 0, 255)); /* Bottom-right now has Green (was top-right) */

	image_destroy(img);
}

/**
 * @test Test orientation=5 (transpose)
 *
 * Verifies transpose transformation (flip horizontal + rotate 270° CW).
 * Dimensions should be swapped.
 */
CTEST(image_orientation, orientation_5_transpose)
{
	image_t *img = create_test_pattern(4, 4);
	ASSERT_NOT_NULL(img);

	/* Apply orientation 5 (transpose) */
	int result = image_apply_orientation(img, 5);
	ASSERT_EQUAL(0, result);

	/* Verify dimensions unchanged for square image */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	/* Verify transpose transformation (flip horizontal + rotate 270° CW) */
	ASSERT_TRUE(check_pixel(img, 0, 0, 255, 0, 0, 255)); /* Top-left: Red */
	ASSERT_TRUE(check_pixel(img, 3, 0, 0, 0, 255, 255)); /* Top-right: Blue */
	ASSERT_TRUE(check_pixel(img, 0, 3, 0, 255, 0, 255)); /* Bottom-left: Green */
	ASSERT_TRUE(check_pixel(img, 3, 3, 255, 255, 0, 255)); /* Bottom-right: Yellow */

	image_destroy(img);
}

/**
 * @test Test orientation=6 (rotate 90° CW)
 *
 * Verifies 90 degree clockwise rotation.
 * Dimensions should be swapped for non-square images.
 */
CTEST(image_orientation, orientation_6_rotate_90cw)
{
	/* Test with non-square image to verify dimension swap */
	image_t *img = create_test_pattern(6, 4);
	ASSERT_NOT_NULL(img);

	/* Apply orientation 6 (rotate 90° CW) */
	int result = image_apply_orientation(img, 6);
	ASSERT_EQUAL(0, result);

	/* Verify dimensions swapped: 6x4 → 4x6 */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(6, img->height);

	/* Verify rotation: top-left → top-right, top-right → bottom-right, etc. */
	ASSERT_TRUE(check_pixel(img, 0, 0, 0, 0, 255, 255)); /* Top-left (was bottom-left) */
	ASSERT_TRUE(check_pixel(img, 3, 0, 255, 0, 0, 255)); /* Top-right (was top-left) */
	ASSERT_TRUE(check_pixel(img, 0, 5, 255, 255, 0, 255)); /* Bottom-left (was bottom-right) */
	ASSERT_TRUE(check_pixel(img, 3, 5, 0, 255, 0, 255)); /* Bottom-right (was top-right) */

	image_destroy(img);
}

/**
 * @test Test orientation=7 (transverse)
 *
 * Verifies transverse transformation (flip horizontal + rotate 90° CW).
 * Dimensions should be swapped.
 */
CTEST(image_orientation, orientation_7_transverse)
{
	image_t *img = create_test_pattern(4, 4);
	ASSERT_NOT_NULL(img);

	/* Apply orientation 7 (transverse) */
	int result = image_apply_orientation(img, 7);
	ASSERT_EQUAL(0, result);

	/* Verify dimensions unchanged for square image */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);

	/* Verify transverse transformation (flip horizontal + rotate 90° CW) */
	ASSERT_TRUE(check_pixel(img, 0, 0, 255, 255, 0, 255)); /* Top-left: Yellow */
	ASSERT_TRUE(check_pixel(img, 3, 0, 0, 255, 0, 255)); /* Top-right: Green */
	ASSERT_TRUE(check_pixel(img, 0, 3, 0, 0, 255, 255)); /* Bottom-left: Blue */
	ASSERT_TRUE(check_pixel(img, 3, 3, 255, 0, 0, 255)); /* Bottom-right: Red */

	image_destroy(img);
}

/**
 * @test Test orientation=8 (rotate 270° CW)
 *
 * Verifies 270 degree clockwise rotation.
 * Dimensions should be swapped for non-square images.
 */
CTEST(image_orientation, orientation_8_rotate_270cw)
{
	/* Test with non-square image to verify dimension swap */
	image_t *img = create_test_pattern(6, 4);
	ASSERT_NOT_NULL(img);

	/* Apply orientation 8 (rotate 270° CW) */
	int result = image_apply_orientation(img, 8);
	ASSERT_EQUAL(0, result);

	/* Verify dimensions swapped: 6x4 → 4x6 */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(6, img->height);

	/* Verify rotation: top-left → bottom-left, top-right → top-left, etc. */
	ASSERT_TRUE(check_pixel(img, 0, 0, 0, 255, 0, 255)); /* Top-left (was top-right) */
	ASSERT_TRUE(check_pixel(img, 3, 0, 255, 255, 0, 255)); /* Top-right (was bottom-right) */
	ASSERT_TRUE(check_pixel(img, 0, 5, 255, 0, 0, 255)); /* Bottom-left (was top-left) */
	ASSERT_TRUE(check_pixel(img, 3, 5, 0, 0, 255, 255)); /* Bottom-right (was bottom-left) */

	image_destroy(img);
}

/**
 * @test Test invalid orientation values
 *
 * Verifies that invalid orientation values are rejected.
 */
CTEST(image_orientation, invalid_orientation_value)
{
	image_t *img = create_test_pattern(4, 4);
	ASSERT_NOT_NULL(img);

	/* Test orientation=0 (invalid) */
	int result = image_apply_orientation(img, 0);
	ASSERT_EQUAL(-1, result);

	/* Verify image unchanged */
	ASSERT_EQUAL(4, img->width);
	ASSERT_EQUAL(4, img->height);
	ASSERT_TRUE(check_pixel(img, 0, 0, 255, 0, 0, 255)); /* Top-left: Red */

	/* Test orientation=9 (invalid) */
	result = image_apply_orientation(img, 9);
	ASSERT_EQUAL(-1, result);

	/* Test orientation=255 (invalid) */
	result = image_apply_orientation(img, 255);
	ASSERT_EQUAL(-1, result);

	image_destroy(img);
}

/**
 * @test Test NULL image pointer
 *
 * Verifies that NULL image pointer is handled gracefully.
 */
CTEST(image_orientation, null_image_pointer)
{
	/* Call with NULL image should return -1 */
	int result = image_apply_orientation(NULL, 6);
	ASSERT_EQUAL(-1, result);
}

/**
 * @test Test NULL pixel buffer
 *
 * Verifies that image with NULL pixel buffer is handled gracefully.
 */
CTEST(image_orientation, null_pixel_buffer)
{
	/* Create image structure with NULL pixels */
	image_t img;
	img.width = 4;
	img.height = 4;
	img.pixels = NULL;
	img.exif = NULL;
	img.xmp = NULL;

	/* Call should return -1 */
	int result = image_apply_orientation(&img, 6);
	ASSERT_EQUAL(-1, result);
}
