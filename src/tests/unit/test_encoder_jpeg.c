/**
 * @file test_encoder_jpeg.c
 * @brief Unit tests for JPEG encoder
 *
 * Tests JPEG encoding functionality using ctest.h framework.
 * Tests quality settings, input validation, and output format.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

/* Forward declaration for JPEG encoder function */
extern int encode_jpeg(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);

/* JPEG magic bytes (Start Of Image marker) */
#ifndef JPEG_SOI_MARKER_1
#define JPEG_SOI_MARKER_1 0xFF
#endif
#ifndef JPEG_SOI_MARKER_2
#define JPEG_SOI_MARKER_2 0xD8
#endif

/**
 * @test Test encode_jpeg() with NULL parameters
 *
 * Verifies that encode_jpeg() rejects NULL input gracefully.
 */
CTEST(encoder_jpeg, null_inputs)
{
	uint8_t *out_data = NULL;
	size_t out_size = 0;

	/* Create minimal valid image */
	image_t *img = image_create(1, 1);
	ASSERT_NOT_NULL(img);

	/* NULL image pointer */
	int result = encode_jpeg(NULL, 80, &out_data, &out_size);
	ASSERT_EQUAL(-1, result);
	ASSERT_NULL(out_data);
	ASSERT_EQUAL(0, out_size);

	/* NULL out_data pointer */
	result = encode_jpeg(img, 80, NULL, &out_size);
	ASSERT_EQUAL(-1, result);

	/* NULL out_size pointer */
	result = encode_jpeg(img, 80, &out_data, NULL);
	ASSERT_EQUAL(-1, result);

	image_destroy(img);
}

/**
 * @test Test encode_jpeg() with minimal 1x1 image
 *
 * Verifies that JPEG encoder can encode the smallest possible image.
 */
CTEST(encoder_jpeg, encode_1x1_image)
{
	/* Create 1x1 red image */
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

	int result = encode_jpeg(img, 80, &out_data, &out_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data);
	ASSERT_TRUE(out_size > 0);

	/* Verify JPEG magic bytes (SOI marker: 0xFF 0xD8) */
	ASSERT_EQUAL(JPEG_SOI_MARKER_1, out_data[0]);
	ASSERT_EQUAL(JPEG_SOI_MARKER_2, out_data[1]);

	free(out_data);
	image_destroy(img);
}

/**
 * @test Test encode_jpeg() with 100x100 RGBA image
 *
 * Verifies that JPEG encoder can encode a typical image.
 */
CTEST(encoder_jpeg, encode_basic)
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

	int result = encode_jpeg(img, 80, &out_data, &out_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data);
	ASSERT_TRUE(out_size > 0);

	/* Verify JPEG magic bytes */
	ASSERT_EQUAL(JPEG_SOI_MARKER_1, out_data[0]);
	ASSERT_EQUAL(JPEG_SOI_MARKER_2, out_data[1]);

	/* JPEG output should be reasonably sized (between 500 bytes and 50KB for 100x100) */
	ASSERT_TRUE(out_size >= 500);
	ASSERT_TRUE(out_size <= 50000);

	free(out_data);
	image_destroy(img);
}

/**
 * @test Test encode_jpeg() with different quality levels
 *
 * Verifies that quality parameter affects output size as expected.
 * Higher quality should produce larger files.
 */
CTEST(encoder_jpeg, quality_range)
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

	uint8_t *out_data_q0 = NULL;
	size_t out_size_q0 = 0;
	uint8_t *out_data_q50 = NULL;
	size_t out_size_q50 = 0;
	uint8_t *out_data_q100 = NULL;
	size_t out_size_q100 = 0;

	/* Encode with quality 0 (minimum) */
	int result = encode_jpeg(img, 0, &out_data_q0, &out_size_q0);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_q0);
	ASSERT_TRUE(out_size_q0 > 0);

	/* Encode with quality 50 (medium) */
	result = encode_jpeg(img, 50, &out_data_q50, &out_size_q50);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_q50);
	ASSERT_TRUE(out_size_q50 > 0);

	/* Encode with quality 100 (maximum) */
	result = encode_jpeg(img, 100, &out_data_q100, &out_size_q100);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_q100);
	ASSERT_TRUE(out_size_q100 > 0);

	/* Verify all outputs are valid JPEG */
	ASSERT_EQUAL(JPEG_SOI_MARKER_1, out_data_q0[0]);
	ASSERT_EQUAL(JPEG_SOI_MARKER_2, out_data_q0[1]);
	ASSERT_EQUAL(JPEG_SOI_MARKER_1, out_data_q50[0]);
	ASSERT_EQUAL(JPEG_SOI_MARKER_2, out_data_q50[1]);
	ASSERT_EQUAL(JPEG_SOI_MARKER_1, out_data_q100[0]);
	ASSERT_EQUAL(JPEG_SOI_MARKER_2, out_data_q100[1]);

	/* Verify quality affects file size: q0 < q50 < q100 */
	ASSERT_TRUE(out_size_q0 < out_size_q50);
	ASSERT_TRUE(out_size_q50 < out_size_q100);

	free(out_data_q0);
	free(out_data_q50);
	free(out_data_q100);
	image_destroy(img);
}

/**
 * @test Test encode_jpeg() quality clamping
 *
 * Verifies that quality values outside [0, 100] are clamped correctly.
 */
CTEST(encoder_jpeg, quality_clamping)
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
	uint8_t *out_data_q0 = NULL;
	size_t out_size_q0 = 0;
	uint8_t *out_data_q100 = NULL;
	size_t out_size_q100 = 0;

	/* Test negative quality (should clamp to 0) */
	int result = encode_jpeg(img, -50, &out_data_neg, &out_size_neg);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_neg);
	ASSERT_TRUE(out_size_neg > 0);

	/* Test quality > 100 (should clamp to 100) */
	result = encode_jpeg(img, 150, &out_data_high, &out_size_high);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_high);
	ASSERT_TRUE(out_size_high > 0);

	/* Test quality 0 for comparison */
	result = encode_jpeg(img, 0, &out_data_q0, &out_size_q0);
	ASSERT_EQUAL(0, result);

	/* Test quality 100 for comparison */
	result = encode_jpeg(img, 100, &out_data_q100, &out_size_q100);
	ASSERT_EQUAL(0, result);

	/* Negative should produce same size as quality 0 */
	ASSERT_EQUAL(out_size_q0, out_size_neg);

	/* >100 should produce same size as quality 100 */
	ASSERT_EQUAL(out_size_q100, out_size_high);

	free(out_data_neg);
	free(out_data_high);
	free(out_data_q0);
	free(out_data_q100);
	image_destroy(img);
}

/**
 * @test Test JPEG magic bytes verification
 *
 * Verifies that JPEG output always starts with correct magic bytes (0xFF 0xD8).
 */
CTEST(encoder_jpeg, magic_bytes)
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

	int result = encode_jpeg(img, 75, &out_data, &out_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data);
	ASSERT_TRUE(out_size >= 2); /* Must have at least 2 bytes for SOI marker */

	/* Verify JPEG Start Of Image (SOI) marker: 0xFF 0xD8 */
	ASSERT_EQUAL(JPEG_SOI_MARKER_1, out_data[0]);
	ASSERT_EQUAL(JPEG_SOI_MARKER_2, out_data[1]);

	free(out_data);
	image_destroy(img);
}
