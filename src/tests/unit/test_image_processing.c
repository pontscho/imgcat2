/**
 * @file test_image_processing.c
 * @brief Unit tests for image processing (scaling, format conversion)
 *
 * Tests image scaling (fit, resize) and color format conversions.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

/**
 * @test Test image_scale_fit() maintains aspect ratio
 *
 * Verifies that image_scale_fit() scales to fit within bounds
 * while maintaining the original aspect ratio.
 */
CTEST(image_proc, scale_fit_maintains_aspect)
{
	/* Create 4x2 image (2:1 aspect ratio) */
	image_t *src = image_create(4, 2);
	ASSERT_NOT_NULL(src);

	/* Scale to fit within 10x10 - should be 10x5 (2:1 maintained) */
	image_t *scaled = image_scale_fit(src, 10, 10);
	ASSERT_NOT_NULL(scaled);
	ASSERT_EQUAL(10, scaled->width);
	ASSERT_EQUAL(5, scaled->height);

	image_destroy(scaled);
	image_destroy(src);
}

/**
 * @test Test image_scale_fit() with tall image
 *
 * Verifies scaling for images taller than wide.
 */
CTEST(image_proc, scale_fit_tall_image)
{
	/* Create 2x4 image (1:2 aspect ratio) */
	image_t *src = image_create(2, 4);
	ASSERT_NOT_NULL(src);

	/* Scale to fit within 10x10 - should be 5x10 (1:2 maintained) */
	image_t *scaled = image_scale_fit(src, 10, 10);
	ASSERT_NOT_NULL(scaled);
	ASSERT_EQUAL(5, scaled->width);
	ASSERT_EQUAL(10, scaled->height);

	image_destroy(scaled);
	image_destroy(src);
}

/**
 * @test Test image_scale_fit() with NULL
 *
 * Verifies graceful handling of NULL input.
 */
CTEST(image_proc, scale_fit_null)
{
	image_t *scaled = image_scale_fit(NULL, 10, 10);
	ASSERT_NULL(scaled);
}

/**
 * @test Test image_scale_resize() to exact dimensions
 *
 * Verifies that image_scale_resize() scales to exact dimensions
 * without maintaining aspect ratio.
 */
CTEST(image_proc, scale_resize_exact)
{
	/* Create 4x2 image */
	image_t *src = image_create(4, 2);
	ASSERT_NOT_NULL(src);

	/* Resize to exactly 8x8 (distorts 2:1 → 1:1) */
	image_t *resized = image_scale_resize(src, 8, 8);
	ASSERT_NOT_NULL(resized);
	ASSERT_EQUAL(8, resized->width);
	ASSERT_EQUAL(8, resized->height);

	image_destroy(resized);
	image_destroy(src);
}

/**
 * @test Test image_scale_resize() with NULL
 *
 * Verifies graceful handling of NULL input.
 */
CTEST(image_proc, scale_resize_null)
{
	image_t *resized = image_scale_resize(NULL, 10, 10);
	ASSERT_NULL(resized);
}

/**
 * @test Test image_scale_fit() with same dimensions
 *
 * Verifies scaling when target dimensions match source.
 */
CTEST(image_proc, scale_fit_same_size)
{
	image_t *src = image_create(10, 10);
	ASSERT_NOT_NULL(src);

	/* Scale to same size */
	image_t *scaled = image_scale_fit(src, 10, 10);
	ASSERT_NOT_NULL(scaled);
	ASSERT_EQUAL(10, scaled->width);
	ASSERT_EQUAL(10, scaled->height);

	image_destroy(scaled);
	image_destroy(src);
}

/**
 * @test Test convert_rgb_to_rgba() functionality
 *
 * Verifies RGB (3 channels) to RGBA (4 channels) conversion.
 */
CTEST(image_proc, rgb_to_rgba_conversion)
{
	/* 2x2 RGB image (3 bytes per pixel = 12 bytes total) */
	uint8_t rgb_data[12] = {
		255, 0,   0, /* Red */
		0,   255, 0, /* Green */
		0,   0,   255, /* Blue */
		255, 255, 255 /* White */
	};

	image_t *rgba = convert_rgb_to_rgba(rgb_data, 2, 2);
	ASSERT_NOT_NULL(rgba);
	ASSERT_EQUAL(2, rgba->width);
	ASSERT_EQUAL(2, rgba->height);

	/* Verify red pixel (0,0) */
	uint8_t *pixel = image_get_pixel(rgba, 0, 0);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(255, pixel[0]); /* R */
	ASSERT_EQUAL(0, pixel[1]); /* G */
	ASSERT_EQUAL(0, pixel[2]); /* B */
	ASSERT_EQUAL(255, pixel[3]); /* A = opaque */

	/* Verify green pixel (1,0) */
	pixel = image_get_pixel(rgba, 1, 0);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(0, pixel[0]);
	ASSERT_EQUAL(255, pixel[1]);
	ASSERT_EQUAL(0, pixel[2]);
	ASSERT_EQUAL(255, pixel[3]);

	image_destroy(rgba);
}

/**
 * @test Test convert_rgb_to_rgba() with NULL
 *
 * Verifies graceful handling of NULL input.
 */
CTEST(image_proc, rgb_to_rgba_null)
{
	image_t *rgba = convert_rgb_to_rgba(NULL, 2, 2);
	ASSERT_NULL(rgba);
}

/**
 * @test Test convert_grayscale_to_rgba() functionality
 *
 * Verifies grayscale (1 channel) to RGBA (4 channels) conversion.
 */
CTEST(image_proc, grayscale_to_rgba_conversion)
{
	/* 2x2 grayscale image (1 byte per pixel = 4 bytes total) */
	uint8_t gray_data[4] = { 0, 127, 200, 255 };

	image_t *rgba = convert_grayscale_to_rgba(gray_data, 2, 2);
	ASSERT_NOT_NULL(rgba);
	ASSERT_EQUAL(2, rgba->width);
	ASSERT_EQUAL(2, rgba->height);

	/* Verify black pixel (0,0) */
	uint8_t *pixel = image_get_pixel(rgba, 0, 0);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(0, pixel[0]); /* R = gray */
	ASSERT_EQUAL(0, pixel[1]); /* G = gray */
	ASSERT_EQUAL(0, pixel[2]); /* B = gray */
	ASSERT_EQUAL(255, pixel[3]); /* A = opaque */

	/* Verify mid-gray pixel (1,0) */
	pixel = image_get_pixel(rgba, 1, 0);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(127, pixel[0]); /* R = gray */
	ASSERT_EQUAL(127, pixel[1]); /* G = gray */
	ASSERT_EQUAL(127, pixel[2]); /* B = gray */
	ASSERT_EQUAL(255, pixel[3]);

	image_destroy(rgba);
}

/**
 * @test Test convert_grayscale_to_rgba() with NULL
 *
 * Verifies graceful handling of NULL input.
 */
CTEST(image_proc, grayscale_to_rgba_null)
{
	image_t *rgba = convert_grayscale_to_rgba(NULL, 2, 2);
	ASSERT_NULL(rgba);
}

/**
 * @test Test image_scale_fit() downscaling
 *
 * Verifies that downscaling works correctly.
 */
CTEST(image_proc, scale_fit_downscale)
{
	/* Create 100x50 image */
	image_t *src = image_create(100, 50);
	ASSERT_NOT_NULL(src);

	/* Scale down to fit within 20x20 - should be 20x10 */
	image_t *scaled = image_scale_fit(src, 20, 20);
	ASSERT_NOT_NULL(scaled);
	ASSERT_EQUAL(20, scaled->width);
	ASSERT_EQUAL(10, scaled->height);

	image_destroy(scaled);
	image_destroy(src);
}

/**
 * @test Test image_scale_fit() upscaling
 *
 * Verifies that upscaling works correctly.
 */
CTEST(image_proc, scale_fit_upscale)
{
	/* Create 10x5 image */
	image_t *src = image_create(10, 5);
	ASSERT_NOT_NULL(src);

	/* Scale up to fit within 100x100 - should be 100x50 */
	image_t *scaled = image_scale_fit(src, 100, 100);
	ASSERT_NOT_NULL(scaled);
	ASSERT_EQUAL(100, scaled->width);
	ASSERT_EQUAL(50, scaled->height);

	image_destroy(scaled);
	image_destroy(src);
}

/**
 * @test Test image_calculate_size() with valid dimensions
 *
 * Verifies size calculation for image pixel data.
 */
CTEST(image_proc, calculate_size_valid)
{
	size_t size;
	bool result;

	/* 10x10 image = 400 bytes (10 * 10 * 4) */
	result = image_calculate_size(10, 10, &size);
	ASSERT_TRUE(result);
	ASSERT_EQUAL(400, size);

	/* 100x200 image = 80000 bytes */
	result = image_calculate_size(100, 200, &size);
	ASSERT_TRUE(result);
	ASSERT_EQUAL(80000, size);
}

/**
 * @test Test image_calculate_size() with NULL output
 *
 * Verifies handling of NULL output parameter.
 */
CTEST(image_proc, calculate_size_null_output)
{
	bool result = image_calculate_size(10, 10, NULL);
	ASSERT_FALSE(result);
}

/**
 * @test Test image_calculate_size() with overflow
 *
 * Verifies overflow detection in size calculation.
 */
CTEST(image_proc, calculate_size_overflow)
{
	size_t size;

	/* Try to overflow: very large dimensions */
	bool result = image_calculate_size(UINT32_MAX, UINT32_MAX, &size);
	ASSERT_FALSE(result); /* Should detect overflow */

	/* Try dimensions that exceed IMAGE_MAX_PIXELS */
	result = image_calculate_size(20000, 20000, &size);
	ASSERT_FALSE(result); /* 400M pixels > 100M limit */
}
