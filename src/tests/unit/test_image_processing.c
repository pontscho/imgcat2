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

#include "../../libimg2/core/image.h"
#include "../ctest.h"

/**
 * @test Test img2_image_scale_fit() maintains aspect ratio
 *
 * Verifies that img2_image_scale_fit() scales to fit within bounds
 * while maintaining the original aspect ratio.
 */
CTEST(image_proc, scale_fit_maintains_aspect)
{
	/* Create 4x2 image (2:1 aspect ratio) */
	img2_image_t *src = img2_image_alloc(4, 2);
	ASSERT_NOT_NULL(src);

	/* Scale to fit within 10x10 - should be 10x5 (2:1 maintained) */
	img2_image_t *scaled = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_scale_fit(src, 10, 10, &scaled));
	ASSERT_NOT_NULL(scaled);
	ASSERT_EQUAL(10, scaled->width);
	ASSERT_EQUAL(5, scaled->height);

	img2_image_destroy(scaled);
	img2_image_destroy(src);
}

/**
 * @test Test img2_image_scale_fit() with tall image
 *
 * Verifies scaling for images taller than wide.
 */
CTEST(image_proc, scale_fit_tall_image)
{
	/* Create 2x4 image (1:2 aspect ratio) */
	img2_image_t *src = img2_image_alloc(2, 4);
	ASSERT_NOT_NULL(src);

	/* Scale to fit within 10x10 - should be 5x10 (1:2 maintained) */
	img2_image_t *scaled = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_scale_fit(src, 10, 10, &scaled));
	ASSERT_NOT_NULL(scaled);
	ASSERT_EQUAL(5, scaled->width);
	ASSERT_EQUAL(10, scaled->height);

	img2_image_destroy(scaled);
	img2_image_destroy(src);
}

/**
 * @test Test img2_image_scale_fit() with NULL
 *
 * Verifies graceful handling of NULL input.
 */
CTEST(image_proc, scale_fit_null)
{
	img2_image_t *scaled = (img2_image_t *)1;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_fit(NULL, 10, 10, &scaled));
	ASSERT_NULL(scaled);
}

/**
 * @test Test img2_image_scale_resize() to exact dimensions
 *
 * Verifies that img2_image_scale_resize() scales to exact dimensions
 * without maintaining aspect ratio.
 */
CTEST(image_proc, scale_resize_exact)
{
	/* Create 4x2 image */
	img2_image_t *src = img2_image_alloc(4, 2);
	ASSERT_NOT_NULL(src);

	/* Resize to exactly 8x8 (distorts 2:1 → 1:1) */
	img2_image_t *resized = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_scale_resize(src, 8, 8, &resized));
	ASSERT_NOT_NULL(resized);
	ASSERT_EQUAL(8, resized->width);
	ASSERT_EQUAL(8, resized->height);

	img2_image_destroy(resized);
	img2_image_destroy(src);
}

/**
 * @test Test img2_image_scale_resize() with NULL
 *
 * Verifies graceful handling of NULL input.
 */
CTEST(image_proc, scale_resize_null)
{
	img2_image_t *resized = (img2_image_t *)1;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_resize(NULL, 10, 10, &resized));
	ASSERT_NULL(resized);
}

/**
 * @test Test img2_image_scale_fit() with same dimensions
 *
 * Verifies scaling when target dimensions match source.
 */
CTEST(image_proc, scale_fit_same_size)
{
	img2_image_t *src = img2_image_alloc(10, 10);
	ASSERT_NOT_NULL(src);

	/* Scale to same size */
	img2_image_t *scaled = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_scale_fit(src, 10, 10, &scaled));
	ASSERT_NOT_NULL(scaled);
	ASSERT_EQUAL(10, scaled->width);
	ASSERT_EQUAL(10, scaled->height);

	img2_image_destroy(scaled);
	img2_image_destroy(src);
}

/**
 * @test Test img2_convert_rgb_to_rgba() functionality
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

	img2_image_t *rgba = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_convert_rgb_to_rgba(rgb_data, 2, 2, &rgba));
	ASSERT_NOT_NULL(rgba);
	ASSERT_EQUAL(2, rgba->width);
	ASSERT_EQUAL(2, rgba->height);

	/* Verify red pixel (0,0) */
	uint8_t *pixel = img2_image_get_pixel(rgba, 0, 0);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(255, pixel[0]); /* R */
	ASSERT_EQUAL(0, pixel[1]); /* G */
	ASSERT_EQUAL(0, pixel[2]); /* B */
	ASSERT_EQUAL(255, pixel[3]); /* A = opaque */

	/* Verify green pixel (1,0) */
	pixel = img2_image_get_pixel(rgba, 1, 0);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(0, pixel[0]);
	ASSERT_EQUAL(255, pixel[1]);
	ASSERT_EQUAL(0, pixel[2]);
	ASSERT_EQUAL(255, pixel[3]);

	img2_image_destroy(rgba);
}

/**
 * @test Test img2_convert_rgb_to_rgba() with NULL
 *
 * Verifies graceful handling of NULL input.
 */
CTEST(image_proc, rgb_to_rgba_null)
{
	img2_image_t *rgba = (img2_image_t *)1;
	ASSERT_EQUAL(IMG2_E_ARG, img2_convert_rgb_to_rgba(NULL, 2, 2, &rgba));
	ASSERT_NULL(rgba);
}

/**
 * @test Test img2_convert_gray_to_rgba() functionality
 *
 * Verifies grayscale (1 channel) to RGBA (4 channels) conversion.
 */
CTEST(image_proc, grayscale_to_rgba_conversion)
{
	/* 2x2 grayscale image (1 byte per pixel = 4 bytes total) */
	uint8_t gray_data[4] = { 0, 127, 200, 255 };

	img2_image_t *rgba = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_convert_gray_to_rgba(gray_data, 2, 2, &rgba));
	ASSERT_NOT_NULL(rgba);
	ASSERT_EQUAL(2, rgba->width);
	ASSERT_EQUAL(2, rgba->height);

	/* Verify black pixel (0,0) */
	uint8_t *pixel = img2_image_get_pixel(rgba, 0, 0);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(0, pixel[0]); /* R = gray */
	ASSERT_EQUAL(0, pixel[1]); /* G = gray */
	ASSERT_EQUAL(0, pixel[2]); /* B = gray */
	ASSERT_EQUAL(255, pixel[3]); /* A = opaque */

	/* Verify mid-gray pixel (1,0) */
	pixel = img2_image_get_pixel(rgba, 1, 0);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(127, pixel[0]); /* R = gray */
	ASSERT_EQUAL(127, pixel[1]); /* G = gray */
	ASSERT_EQUAL(127, pixel[2]); /* B = gray */
	ASSERT_EQUAL(255, pixel[3]);

	img2_image_destroy(rgba);
}

/**
 * @test Test img2_convert_gray_to_rgba() with NULL
 *
 * Verifies graceful handling of NULL input.
 */
CTEST(image_proc, grayscale_to_rgba_null)
{
	img2_image_t *rgba = (img2_image_t *)1;
	ASSERT_EQUAL(IMG2_E_ARG, img2_convert_gray_to_rgba(NULL, 2, 2, &rgba));
	ASSERT_NULL(rgba);
}

/**
 * @test Test img2_image_scale_fit() downscaling
 *
 * Verifies that downscaling works correctly.
 */
CTEST(image_proc, scale_fit_downscale)
{
	/* Create 100x50 image */
	img2_image_t *src = img2_image_alloc(100, 50);
	ASSERT_NOT_NULL(src);

	/* Scale down to fit within 20x20 - should be 20x10 */
	img2_image_t *scaled = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_scale_fit(src, 20, 20, &scaled));
	ASSERT_NOT_NULL(scaled);
	ASSERT_EQUAL(20, scaled->width);
	ASSERT_EQUAL(10, scaled->height);

	img2_image_destroy(scaled);
	img2_image_destroy(src);
}

/**
 * @test Test img2_image_scale_fit() upscaling
 *
 * Verifies that upscaling works correctly.
 */
CTEST(image_proc, scale_fit_upscale)
{
	/* Create 10x5 image */
	img2_image_t *src = img2_image_alloc(10, 5);
	ASSERT_NOT_NULL(src);

	/* Scale up to fit within 100x100 - should be 100x50 */
	img2_image_t *scaled = NULL;
	ASSERT_EQUAL(IMG2_OK, img2_image_scale_fit(src, 100, 100, &scaled));
	ASSERT_NOT_NULL(scaled);
	ASSERT_EQUAL(100, scaled->width);
	ASSERT_EQUAL(50, scaled->height);

	img2_image_destroy(scaled);
	img2_image_destroy(src);
}

/**
 * @test Test img2_image_calc_size() with valid dimensions
 *
 * Verifies size calculation for image pixel data.
 */
CTEST(image_proc, calculate_size_valid)
{
	size_t size;
	bool result;

	/* 10x10 image = 400 bytes (10 * 10 * 4) */
	result = img2_image_calc_size(10, 10, &size);
	ASSERT_TRUE(result);
	ASSERT_EQUAL(400, size);

	/* 100x200 image = 80000 bytes */
	result = img2_image_calc_size(100, 200, &size);
	ASSERT_TRUE(result);
	ASSERT_EQUAL(80000, size);
}

/**
 * @test Test img2_image_calc_size() with NULL output
 *
 * Verifies handling of NULL output parameter.
 */
CTEST(image_proc, calculate_size_null_output)
{
	bool result = img2_image_calc_size(10, 10, NULL);
	ASSERT_FALSE(result);
}

/**
 * @test Test img2_image_calc_size() with overflow
 *
 * Verifies overflow detection in size calculation.
 */
CTEST(image_proc, calculate_size_overflow)
{
	size_t size;

	/* Try to overflow: very large dimensions */
	bool result = img2_image_calc_size(UINT32_MAX, UINT32_MAX, &size);
	ASSERT_FALSE(result); /* Should detect overflow */

	/* Try dimensions that exceed IMG2_MAX_PIXELS */
	result = img2_image_calc_size(20000, 20000, &size);
	ASSERT_FALSE(result); /* 400M pixels > 100M limit */
}

/**
 * @test [M7] status codes of the scale helpers and [SM2] img2_fit_size
 */
CTEST(image_proc, scale_status_codes)
{
	img2_image_t *src = img2_image_alloc(4, 2);
	ASSERT_NOT_NULL(src);
	ASSERT_EQUAL(16, src->stride);
	ASSERT_EQUAL(0, src->delay_ms);

	img2_image_t *out = (img2_image_t *)1;
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_resize(src, 0, 10, &out));
	ASSERT_NULL(out);
	ASSERT_EQUAL(IMG2_E_LIMIT, img2_image_scale_resize(src, 70000, 1, &out));
	ASSERT_NULL(out);
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_fit(src, 10, 0, &out));
	ASSERT_EQUAL(IMG2_E_ARG, img2_image_scale_fit(src, 10, 10, NULL));
	ASSERT_EQUAL(IMG2_OK, img2_image_scale_resize(src, 3, 7, &out));
	ASSERT_NOT_NULL(out);
	ASSERT_EQUAL(12, out->stride);
	img2_image_destroy(out);
	img2_image_destroy(src);

	uint32_t w = 0, h = 0;
	ASSERT_EQUAL(IMG2_E_ARG, img2_fit_size(0, 10, 10, 10, &w, &h));
	ASSERT_EQUAL(IMG2_E_ARG, img2_fit_size(10, 10, 0, 10, &w, &h));
	ASSERT_EQUAL(IMG2_OK, img2_fit_size(4, 2, 10, 10, &w, &h));
	ASSERT_EQUAL(10, w);
	ASSERT_EQUAL(5, h);

	/* 70000x1 is over IMG2_MAX_DIMENSION */
	size_t size = 0;
	ASSERT_FALSE(img2_image_calc_size(70000, 1, &size));
}

/**
 * @test img2_frames_free() is NULL-safe and zeroes the struct
 */
CTEST(image_proc, frames_free)
{
	img2_frames_free(NULL);
	img2_frames_t fr = { 0 };
	fr.frames = malloc(2 * sizeof(img2_image_t *));
	ASSERT_NOT_NULL(fr.frames);
	fr.frames[0] = img2_image_alloc(2, 2);
	fr.frames[1] = img2_image_alloc(2, 2);
	fr.count = 2;
	fr.loop_count = 3;
	img2_frames_free(&fr);
	ASSERT_NULL(fr.frames);
	ASSERT_EQUAL(0, fr.count);
	ASSERT_EQUAL(0, fr.loop_count);
}

/**
 * @test img2_strerror() never returns NULL
 */
CTEST(image_proc, strerror_texts)
{
	ASSERT_NOT_NULL(img2_strerror(IMG2_OK));
	ASSERT_NOT_NULL(img2_strerror(IMG2_E_NOT_FOUND));
	ASSERT_STR("unknown status", img2_strerror((img2_status_t)42));
}
