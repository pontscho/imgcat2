/**
 * @file test_image_create_destroy.c
 * @brief Unit tests for image memory management
 *
 * Tests image_create(), image_destroy(), and memory safety.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

/**
 * @test Test image_create() with valid dimensions
 *
 * Verifies that image_create() successfully allocates an image.
 */
CTEST(image, create_valid_dimensions)
{
	image_t *img = image_create(10, 10);
	ASSERT_NOT_NULL(img);
	ASSERT_EQUAL(10, img->width);
	ASSERT_EQUAL(10, img->height);
	ASSERT_NOT_NULL(img->pixels);

	image_destroy(img);
}

/**
 * @test Test image_create() with 1x1 dimensions
 *
 * Verifies smallest valid image creation.
 */
CTEST(image, create_1x1)
{
	image_t *img = image_create(1, 1);
	ASSERT_NOT_NULL(img);
	ASSERT_EQUAL(1, img->width);
	ASSERT_EQUAL(1, img->height);
	ASSERT_NOT_NULL(img->pixels);

	/* Verify we can access the single pixel */
	uint8_t *pixel = image_get_pixel(img, 0, 0);
	ASSERT_NOT_NULL(pixel);

	/* Verify initial pixel data is zeroed */
	ASSERT_EQUAL(0, pixel[0]); /* R */
	ASSERT_EQUAL(0, pixel[1]); /* G */
	ASSERT_EQUAL(0, pixel[2]); /* B */
	ASSERT_EQUAL(0, pixel[3]); /* A */

	image_destroy(img);
}

/**
 * @test Test image_create() with zero dimensions
 *
 * Verifies that zero dimensions are rejected.
 */
CTEST(image, create_zero_dimensions)
{
	image_t *img;

	/* Zero width */
	img = image_create(0, 10);
	ASSERT_NULL(img);

	/* Zero height */
	img = image_create(10, 0);
	ASSERT_NULL(img);

	/* Both zero */
	img = image_create(0, 0);
	ASSERT_NULL(img);
}

/**
 * @test Test image_create() with dimensions exceeding max
 *
 * Verifies that oversized dimensions are rejected.
 */
CTEST(image, create_exceeds_max_dimension)
{
	/* Exceed IMAGE_MAX_DIMENSION (16384) */
	image_t *img = image_create(IMAGE_MAX_DIMENSION + 1, 10);
	ASSERT_NULL(img);

	img = image_create(10, IMAGE_MAX_DIMENSION + 1);
	ASSERT_NULL(img);

	/* At the limit should succeed */
	img = image_create(IMAGE_MAX_DIMENSION, 1);
	ASSERT_NOT_NULL(img);
	image_destroy(img);
}

/**
 * @test Test image_create() with overflow protection
 *
 * Verifies that multiplication overflow is detected.
 */
CTEST(image, create_overflow_protection)
{
	/* Try to create image that would overflow size calculation */
	/* UINT32_MAX dimensions would overflow width * height * 4 */
	image_t *img = image_create(UINT32_MAX, UINT32_MAX);
	ASSERT_NULL(img);

	/* Try dimensions that exceed IMAGE_MAX_PIXELS (100M) */
	/* 10000 x 10001 = 100,010,000 pixels > 100M */
	img = image_create(10000, 10001);
	ASSERT_NULL(img);
}

/**
 * @test Test image_create() with maximum valid size
 *
 * Verifies that maximum pixel count limit works.
 */
CTEST(image, create_max_valid_size)
{
	/* 10000 x 10000 = 100,000,000 pixels (exactly at limit) */
	image_t *img = image_create(10000, 10000);
	ASSERT_NOT_NULL(img);
	ASSERT_EQUAL(10000, img->width);
	ASSERT_EQUAL(10000, img->height);

	image_destroy(img);
}

/**
 * @test Test image_destroy() with NULL
 *
 * Verifies that image_destroy() handles NULL gracefully.
 */
CTEST(image, destroy_null)
{
	/* Should not crash */
	image_destroy(NULL);
}

/**
 * @test Test image_get_pixel() with valid coordinates
 *
 * Verifies pixel access within bounds.
 */
CTEST(image, get_pixel_valid)
{
	image_t *img = image_create(10, 10);
	ASSERT_NOT_NULL(img);

	/* Access corner pixels */
	ASSERT_NOT_NULL(image_get_pixel(img, 0, 0)); /* Top-left */
	ASSERT_NOT_NULL(image_get_pixel(img, 9, 0)); /* Top-right */
	ASSERT_NOT_NULL(image_get_pixel(img, 0, 9)); /* Bottom-left */
	ASSERT_NOT_NULL(image_get_pixel(img, 9, 9)); /* Bottom-right */
	ASSERT_NOT_NULL(image_get_pixel(img, 5, 5)); /* Center */

	image_destroy(img);
}

/**
 * @test Test image_get_pixel() with out-of-bounds coordinates
 *
 * Verifies that out-of-bounds access returns NULL.
 */
CTEST(image, get_pixel_out_of_bounds)
{
	image_t *img = image_create(10, 10);
	ASSERT_NOT_NULL(img);

	/* Out of bounds access should return NULL */
	ASSERT_NULL(image_get_pixel(img, 10, 0)); /* X out of bounds */
	ASSERT_NULL(image_get_pixel(img, 0, 10)); /* Y out of bounds */
	ASSERT_NULL(image_get_pixel(img, 10, 10)); /* Both out of bounds */
	ASSERT_NULL(image_get_pixel(img, UINT32_MAX, 0)); /* Way out */

	image_destroy(img);
}

/**
 * @test Test image_get_pixel() with NULL image
 *
 * Verifies that NULL image returns NULL.
 */
CTEST(image, get_pixel_null_image)
{
	ASSERT_NULL(image_get_pixel(NULL, 0, 0));
}

/**
 * @test Test image_set_pixel() functionality
 *
 * Verifies setting and reading pixel values.
 */
CTEST(image, set_pixel_valid)
{
	image_t *img = image_create(5, 5);
	ASSERT_NOT_NULL(img);

	/* Set a red pixel at (2, 2) */
	bool result = image_set_pixel(img, 2, 2, 255, 0, 0, 255);
	ASSERT_TRUE(result);

	/* Read it back */
	uint8_t *pixel = image_get_pixel(img, 2, 2);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(255, pixel[0]); /* R */
	ASSERT_EQUAL(0, pixel[1]); /* G */
	ASSERT_EQUAL(0, pixel[2]); /* B */
	ASSERT_EQUAL(255, pixel[3]); /* A */

	image_destroy(img);
}

/**
 * @test Test image_set_pixel() with out-of-bounds
 *
 * Verifies that out-of-bounds set returns false.
 */
CTEST(image, set_pixel_out_of_bounds)
{
	image_t *img = image_create(5, 5);
	ASSERT_NOT_NULL(img);

	/* Out of bounds should fail */
	bool result = image_set_pixel(img, 5, 0, 255, 0, 0, 255);
	ASSERT_FALSE(result);

	result = image_set_pixel(img, 0, 5, 255, 0, 0, 255);
	ASSERT_FALSE(result);

	image_destroy(img);
}

/**
 * @test Test image_set_pixel() with NULL image
 *
 * Verifies that NULL image returns false.
 */
CTEST(image, set_pixel_null_image)
{
	bool result = image_set_pixel(NULL, 0, 0, 255, 0, 0, 255);
	ASSERT_FALSE(result);
}

/**
 * @test Test image pixel data is initialized to zero
 *
 * Verifies that newly created images have zeroed pixel data.
 */
CTEST(image, pixels_initialized_to_zero)
{
	image_t *img = image_create(3, 3);
	ASSERT_NOT_NULL(img);

	/* Check all pixels are zeroed */
	for (uint32_t y = 0; y < 3; y++) {
		for (uint32_t x = 0; x < 3; x++) {
			uint8_t *pixel = image_get_pixel(img, x, y);
			ASSERT_NOT_NULL(pixel);
			ASSERT_EQUAL(0, pixel[0]); /* R */
			ASSERT_EQUAL(0, pixel[1]); /* G */
			ASSERT_EQUAL(0, pixel[2]); /* B */
			ASSERT_EQUAL(0, pixel[3]); /* A */
		}
	}

	image_destroy(img);
}

/**
 * @test Test image_create() with large valid dimensions
 *
 * Verifies that large (but valid) images can be created.
 */
CTEST(image, create_large_valid)
{
	/* 1000 x 1000 = 1M pixels (well under 100M limit) */
	image_t *img = image_create(1000, 1000);
	ASSERT_NOT_NULL(img);
	ASSERT_EQUAL(1000, img->width);
	ASSERT_EQUAL(1000, img->height);

	image_destroy(img);
}

/**
 * @test Test multiple create/destroy cycles
 *
 * Verifies that multiple allocations don't cause issues.
 */
CTEST(image, multiple_create_destroy)
{
	/* Create and destroy multiple images */
	for (int i = 0; i < 10; i++) {
		image_t *img = image_create(100, 100);
		ASSERT_NOT_NULL(img);
		image_destroy(img);
	}
}

/**
 * @test Test pixel memory layout
 *
 * Verifies that pixel data is laid out correctly (row-major, RGBA).
 */
CTEST(image, pixel_memory_layout)
{
	image_t *img = image_create(2, 2);
	ASSERT_NOT_NULL(img);

	/* Set distinct colors for each pixel */
	image_set_pixel(img, 0, 0, 1, 2, 3, 4); /* Top-left */
	image_set_pixel(img, 1, 0, 5, 6, 7, 8); /* Top-right */
	image_set_pixel(img, 0, 1, 9, 10, 11, 12); /* Bottom-left */
	image_set_pixel(img, 1, 1, 13, 14, 15, 16); /* Bottom-right */

	/* Verify memory layout: row-major, 4 bytes per pixel */
	/* Row 0: (0,0) then (1,0) */
	ASSERT_EQUAL(1, img->pixels[0]); /* (0,0) R */
	ASSERT_EQUAL(2, img->pixels[1]); /* (0,0) G */
	ASSERT_EQUAL(3, img->pixels[2]); /* (0,0) B */
	ASSERT_EQUAL(4, img->pixels[3]); /* (0,0) A */
	ASSERT_EQUAL(5, img->pixels[4]); /* (1,0) R */
	ASSERT_EQUAL(6, img->pixels[5]); /* (1,0) G */
	ASSERT_EQUAL(7, img->pixels[6]); /* (1,0) B */
	ASSERT_EQUAL(8, img->pixels[7]); /* (1,0) A */
	/* Row 1: (0,1) then (1,1) */
	ASSERT_EQUAL(9, img->pixels[8]); /* (0,1) R */
	ASSERT_EQUAL(10, img->pixels[9]); /* (0,1) G */
	ASSERT_EQUAL(11, img->pixels[10]); /* (0,1) B */
	ASSERT_EQUAL(12, img->pixels[11]); /* (0,1) A */
	ASSERT_EQUAL(13, img->pixels[12]); /* (1,1) R */
	ASSERT_EQUAL(14, img->pixels[13]); /* (1,1) G */
	ASSERT_EQUAL(15, img->pixels[14]); /* (1,1) B */
	ASSERT_EQUAL(16, img->pixels[15]); /* (1,1) A */

	image_destroy(img);
}
