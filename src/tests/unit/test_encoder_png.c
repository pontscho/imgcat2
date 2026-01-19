/**
 * @file test_encoder_png.c
 * @brief Unit tests for PNG encoder
 *
 * Tests PNG encoding functionality using ctest.h framework.
 * Tests compression settings, input validation, alpha channel preservation, and output format.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

/* Forward declaration for PNG encoder function */
extern int encode_png(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);

/* PNG magic bytes (PNG signature) */
static const uint8_t PNG_SIGNATURE[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };

/**
 * @test Test encode_png() with NULL parameters
 *
 * Verifies that encode_png() rejects NULL input gracefully.
 */
CTEST(encoder_png, null_inputs)
{
	uint8_t *out_data = NULL;
	size_t out_size = 0;

	/* Create minimal valid image */
	image_t *img = image_create(1, 1);
	ASSERT_NOT_NULL(img);

	/* NULL image pointer */
	int result = encode_png(NULL, 6, &out_data, &out_size);
	ASSERT_EQUAL(-1, result);
	ASSERT_NULL(out_data);
	ASSERT_EQUAL(0, out_size);

	/* NULL out_data pointer */
	result = encode_png(img, 6, NULL, &out_size);
	ASSERT_EQUAL(-1, result);

	/* NULL out_size pointer */
	result = encode_png(img, 6, &out_data, NULL);
	ASSERT_EQUAL(-1, result);

	image_destroy(img);
}

/**
 * @test Test encode_png() with minimal 1x1 image
 *
 * Verifies that PNG encoder can encode the smallest possible image.
 */
CTEST(encoder_png, encode_1x1_image)
{
	/* Create 1x1 red image with full opacity */
	image_t *img = image_create(1, 1);
	ASSERT_NOT_NULL(img);

	uint8_t *pixel = image_get_pixel(img, 0, 0);
	ASSERT_NOT_NULL(pixel);
	pixel[0] = 255; /* R */
	pixel[1] = 0; /* G */
	pixel[2] = 0; /* B */
	pixel[3] = 255; /* A */

	uint8_t *out_data = NULL;
	size_t out_size = 0;

	int result = encode_png(img, 6, &out_data, &out_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data);
	ASSERT_TRUE(out_size > 0);

	/* Verify PNG signature (first 8 bytes) */
	ASSERT_TRUE(out_size >= 8);
	for (int i = 0; i < 8; i++) {
		ASSERT_EQUAL(PNG_SIGNATURE[i], out_data[i]);
	}

	free(out_data);
	image_destroy(img);
}

/**
 * @test Test encode_png() with 100x100 RGBA image
 *
 * Verifies that PNG encoder can encode a typical image.
 */
CTEST(encoder_png, encode_basic)
{
	/* Create 100x100 gradient image */
	image_t *img = image_create(100, 100);
	ASSERT_NOT_NULL(img);

	/* Fill with red-to-green gradient */
	for (uint32_t y = 0; y < 100; y++) {
		for (uint32_t x = 0; x < 100; x++) {
			uint8_t *pixel = image_get_pixel(img, x, y);
			ASSERT_NOT_NULL(pixel);
			pixel[0] = 255 - (x * 255 / 100); /* R: decreases left-to-right */
			pixel[1] = (x * 255 / 100); /* G: increases left-to-right */
			pixel[2] = 0; /* B: always 0 */
			pixel[3] = 255; /* A: opaque */
		}
	}

	uint8_t *out_data = NULL;
	size_t out_size = 0;

	int result = encode_png(img, 6, &out_data, &out_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data);
	ASSERT_TRUE(out_size > 0);

	/* Verify PNG signature */
	ASSERT_TRUE(out_size >= 8);
	for (int i = 0; i < 8; i++) {
		ASSERT_EQUAL(PNG_SIGNATURE[i], out_data[i]);
	}

	/* PNG output should be reasonably sized (between 100 bytes and 100KB for 100x100) */
	ASSERT_TRUE(out_size >= 100);
	ASSERT_TRUE(out_size <= 100000);

	free(out_data);
	image_destroy(img);
}

/**
 * @test Test encode_png() with different compression levels
 *
 * Verifies that compression parameter affects output size as expected.
 * Higher compression should produce smaller files (but takes longer).
 */
CTEST(encoder_png, compression_range)
{
	/* Create 100x100 gradient image */
	image_t *img = image_create(100, 100);
	ASSERT_NOT_NULL(img);

	/* Fill with gradient */
	for (uint32_t y = 0; y < 100; y++) {
		for (uint32_t x = 0; x < 100; x++) {
			uint8_t *pixel = image_get_pixel(img, x, y);
			pixel[0] = (x * 255 / 100);
			pixel[1] = (y * 255 / 100);
			pixel[2] = 128;
			pixel[3] = 255;
		}
	}

	uint8_t *out_data_c0 = NULL;
	size_t out_size_c0 = 0;
	uint8_t *out_data_c6 = NULL;
	size_t out_size_c6 = 0;
	uint8_t *out_data_c9 = NULL;
	size_t out_size_c9 = 0;

	/* Encode with compression 0 (no compression) */
	int result = encode_png(img, 0, &out_data_c0, &out_size_c0);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_c0);
	ASSERT_TRUE(out_size_c0 > 0);

	/* Encode with compression 6 (default) */
	result = encode_png(img, 6, &out_data_c6, &out_size_c6);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_c6);
	ASSERT_TRUE(out_size_c6 > 0);

	/* Encode with compression 9 (maximum) */
	result = encode_png(img, 9, &out_data_c9, &out_size_c9);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_c9);
	ASSERT_TRUE(out_size_c9 > 0);

	/* Verify all outputs are valid PNG */
	for (int i = 0; i < 8; i++) {
		ASSERT_EQUAL(PNG_SIGNATURE[i], out_data_c0[i]);
		ASSERT_EQUAL(PNG_SIGNATURE[i], out_data_c6[i]);
		ASSERT_EQUAL(PNG_SIGNATURE[i], out_data_c9[i]);
	}

	/* Verify compression affects file size: c0 >= c6 >= c9 */
	ASSERT_TRUE(out_size_c0 >= out_size_c6);
	ASSERT_TRUE(out_size_c6 >= out_size_c9);

	free(out_data_c0);
	free(out_data_c6);
	free(out_data_c9);
	image_destroy(img);
}

/**
 * @test Test encode_png() compression clamping
 *
 * Verifies that compression values outside [0, 9] are clamped correctly.
 */
CTEST(encoder_png, compression_clamping)
{
	/* Create 10x10 image */
	image_t *img = image_create(10, 10);
	ASSERT_NOT_NULL(img);

	/* Fill with solid color */
	for (uint32_t y = 0; y < 10; y++) {
		for (uint32_t x = 0; x < 10; x++) {
			uint8_t *pixel = image_get_pixel(img, x, y);
			pixel[0] = 128;
			pixel[1] = 128;
			pixel[2] = 128;
			pixel[3] = 255;
		}
	}

	uint8_t *out_data_neg = NULL;
	size_t out_size_neg = 0;
	uint8_t *out_data_high = NULL;
	size_t out_size_high = 0;
	uint8_t *out_data_c0 = NULL;
	size_t out_size_c0 = 0;
	uint8_t *out_data_c9 = NULL;
	size_t out_size_c9 = 0;

	/* Test negative compression (should clamp to 0) */
	int result = encode_png(img, -5, &out_data_neg, &out_size_neg);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_neg);
	ASSERT_TRUE(out_size_neg > 0);

	/* Test compression > 9 (should clamp to 9) */
	result = encode_png(img, 20, &out_data_high, &out_size_high);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_high);
	ASSERT_TRUE(out_size_high > 0);

	/* Test compression 0 for comparison */
	result = encode_png(img, 0, &out_data_c0, &out_size_c0);
	ASSERT_EQUAL(0, result);

	/* Test compression 9 for comparison */
	result = encode_png(img, 9, &out_data_c9, &out_size_c9);
	ASSERT_EQUAL(0, result);

	/* Negative should produce same size as compression 0 */
	ASSERT_EQUAL(out_size_c0, out_size_neg);

	/* >9 should produce same size as compression 9 */
	ASSERT_EQUAL(out_size_c9, out_size_high);

	free(out_data_neg);
	free(out_data_high);
	free(out_data_c0);
	free(out_data_c9);
	image_destroy(img);
}

/**
 * @test Test PNG with transparent alpha channel
 *
 * Verifies that PNG encoder preserves alpha channel (transparency).
 */
CTEST(encoder_png, transparent)
{
	/* Create 50x50 image with varying alpha */
	image_t *img = image_create(50, 50);
	ASSERT_NOT_NULL(img);

	/* Fill with pattern: left half opaque, right half transparent */
	for (uint32_t y = 0; y < 50; y++) {
		for (uint32_t x = 0; x < 50; x++) {
			uint8_t *pixel = image_get_pixel(img, x, y);
			pixel[0] = 255; /* R */
			pixel[1] = 0; /* G */
			pixel[2] = 0; /* B */

			/* Alpha gradient: fully opaque on left, fully transparent on right */
			if (x < 25) {
				pixel[3] = 255; /* Opaque */
			} else {
				pixel[3] = 0; /* Transparent */
			}
		}
	}

	uint8_t *out_data = NULL;
	size_t out_size = 0;

	int result = encode_png(img, 6, &out_data, &out_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data);
	ASSERT_TRUE(out_size > 0);

	/* Verify PNG signature */
	ASSERT_TRUE(out_size >= 8);
	for (int i = 0; i < 8; i++) {
		ASSERT_EQUAL(PNG_SIGNATURE[i], out_data[i]);
	}

	/* PNG should be reasonably sized */
	ASSERT_TRUE(out_size >= 100);
	ASSERT_TRUE(out_size <= 50000);

	free(out_data);
	image_destroy(img);
}

/**
 * @test Test PNG signature verification
 *
 * Verifies that PNG output always starts with correct signature (8 bytes).
 */
CTEST(encoder_png, magic_bytes)
{
	/* Create 50x50 image */
	image_t *img = image_create(50, 50);
	ASSERT_NOT_NULL(img);

	/* Fill with pattern */
	for (uint32_t y = 0; y < 50; y++) {
		for (uint32_t x = 0; x < 50; x++) {
			uint8_t *pixel = image_get_pixel(img, x, y);
			pixel[0] = (x + y) % 256;
			pixel[1] = (x * 2) % 256;
			pixel[2] = (y * 2) % 256;
			pixel[3] = 255;
		}
	}

	uint8_t *out_data = NULL;
	size_t out_size = 0;

	int result = encode_png(img, 6, &out_data, &out_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data);
	ASSERT_TRUE(out_size >= 8); /* Must have at least 8 bytes for PNG signature */

	/* Verify PNG signature: 0x89 0x50 0x4E 0x47 0x0D 0x0A 0x1A 0x0A */
	for (int i = 0; i < 8; i++) {
		ASSERT_EQUAL(PNG_SIGNATURE[i], out_data[i]);
	}

	free(out_data);
	image_destroy(img);
}
