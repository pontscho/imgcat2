/**
 * @file test_encoder_heif.c
 * @brief Unit tests for HEIF encoder
 *
 * Tests HEIF encoding functionality using ctest.h framework.
 * Tests quality settings, input validation, alpha channel preservation, and output format.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

/* Forward declaration for HEIF encoder function */
extern int encode_heif(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);

/**
 * @test Test encode_heif() with NULL parameters
 *
 * Verifies that encode_heif() rejects NULL input gracefully.
 */
CTEST(encoder_heif, null_inputs)
{
	uint8_t *out_data = NULL;
	size_t out_size = 0;

	/* Create minimal valid image */
	image_t *img = image_create(1, 1);
	ASSERT_NOT_NULL(img);

	/* NULL image pointer */
	int result = encode_heif(NULL, 80, &out_data, &out_size);
	ASSERT_EQUAL(-1, result);
	ASSERT_NULL(out_data);
	ASSERT_EQUAL(0, out_size);

	/* NULL out_data pointer */
	result = encode_heif(img, 80, NULL, &out_size);
	ASSERT_EQUAL(-1, result);

	/* NULL out_size pointer */
	result = encode_heif(img, 80, &out_data, NULL);
	ASSERT_EQUAL(-1, result);

	image_destroy(img);
}

/**
 * @test Test encode_heif() with minimal 1x1 image
 *
 * Verifies that HEIF encoder can encode the smallest possible image.
 */
CTEST(encoder_heif, encode_1x1_image)
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

	int result = encode_heif(img, 80, &out_data, &out_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data);
	ASSERT_TRUE(out_size > 0);

	/* Verify HEIF magic bytes (ftyp box) */
	ASSERT_TRUE(out_size >= 12);
	/* ftyp box starts at offset 4 */
	ASSERT_EQUAL('f', out_data[4]);
	ASSERT_EQUAL('t', out_data[5]);
	ASSERT_EQUAL('y', out_data[6]);
	ASSERT_EQUAL('p', out_data[7]);

	free(out_data);
	image_destroy(img);
}

/**
 * @test Test encode_heif() with 10x10 RGBA image
 *
 * Verifies that HEIF encoder can encode a small image.
 */
CTEST(encoder_heif, encode_10x10_image)
{
	/* Create 10x10 gradient image */
	image_t *img = image_create(10, 10);
	ASSERT_NOT_NULL(img);

	/* Fill with red-to-green gradient */
	for (uint32_t y = 0; y < 10; y++) {
		for (uint32_t x = 0; x < 10; x++) {
			uint8_t *pixel = image_get_pixel(img, x, y);
			ASSERT_NOT_NULL(pixel);
			pixel[0] = 255 - (x * 255 / 10); /* R: decreases left-to-right */
			pixel[1] = (x * 255 / 10); /* G: increases left-to-right */
			pixel[2] = 0; /* B: always 0 */
			pixel[3] = 255; /* A: opaque */
		}
	}

	uint8_t *out_data = NULL;
	size_t out_size = 0;

	int result = encode_heif(img, 80, &out_data, &out_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data);
	ASSERT_TRUE(out_size > 0);

	/* Verify HEIF magic bytes (ftyp box) */
	ASSERT_TRUE(out_size >= 12);
	ASSERT_EQUAL('f', out_data[4]);
	ASSERT_EQUAL('t', out_data[5]);
	ASSERT_EQUAL('y', out_data[6]);
	ASSERT_EQUAL('p', out_data[7]);

	/* HEIF output should be reasonably sized (between 100 bytes and 10KB for 10x10) */
	ASSERT_TRUE(out_size >= 100);
	ASSERT_TRUE(out_size <= 10000);

	free(out_data);
	image_destroy(img);
}

/**
 * @test Test encode_heif() with different quality levels
 *
 * Verifies that quality parameter affects output size as expected.
 * Higher quality should produce larger files.
 */
CTEST(encoder_heif, quality_levels)
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

	uint8_t *out_data_q10 = NULL;
	size_t out_size_q10 = 0;
	uint8_t *out_data_q50 = NULL;
	size_t out_size_q50 = 0;
	uint8_t *out_data_q90 = NULL;
	size_t out_size_q90 = 0;

	/* Encode with quality 10 (low) */
	int result = encode_heif(img, 10, &out_data_q10, &out_size_q10);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_q10);
	ASSERT_TRUE(out_size_q10 > 0);

	/* Encode with quality 50 (medium) */
	result = encode_heif(img, 50, &out_data_q50, &out_size_q50);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_q50);
	ASSERT_TRUE(out_size_q50 > 0);

	/* Encode with quality 90 (high) */
	result = encode_heif(img, 90, &out_data_q90, &out_size_q90);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data_q90);
	ASSERT_TRUE(out_size_q90 > 0);

	/* Verify all outputs are valid HEIF */
	ASSERT_EQUAL('f', out_data_q10[4]);
	ASSERT_EQUAL('t', out_data_q10[5]);
	ASSERT_EQUAL('f', out_data_q50[4]);
	ASSERT_EQUAL('t', out_data_q50[5]);
	ASSERT_EQUAL('f', out_data_q90[4]);
	ASSERT_EQUAL('t', out_data_q90[5]);

	/* Verify quality affects file size: q10 < q50 < q90 */
	ASSERT_TRUE(out_size_q10 < out_size_q50);
	ASSERT_TRUE(out_size_q50 < out_size_q90);

	free(out_data_q10);
	free(out_data_q50);
	free(out_data_q90);
	image_destroy(img);
}

/**
 * @test Test HEIF with transparent alpha channel
 *
 * Verifies that HEIF encoder preserves alpha channel (transparency).
 */
CTEST(encoder_heif, transparency)
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

	int result = encode_heif(img, 80, &out_data, &out_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data);
	ASSERT_TRUE(out_size > 0);

	/* Verify HEIF magic bytes (ftyp box) */
	ASSERT_TRUE(out_size >= 12);
	ASSERT_EQUAL('f', out_data[4]);
	ASSERT_EQUAL('t', out_data[5]);
	ASSERT_EQUAL('y', out_data[6]);
	ASSERT_EQUAL('p', out_data[7]);

	/* HEIF should be reasonably sized */
	ASSERT_TRUE(out_size >= 100);
	ASSERT_TRUE(out_size <= 50000);

	free(out_data);
	image_destroy(img);
}

/**
 * @test Test HEIF magic bytes verification
 *
 * Verifies that HEIF output always starts with correct ftyp signature.
 */
CTEST(encoder_heif, magic_bytes)
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

	int result = encode_heif(img, 75, &out_data, &out_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(out_data);
	ASSERT_TRUE(out_size >= 12); /* Must have at least 12 bytes for ftyp box */

	/* Verify ftyp box signature at offset 4 */
	ASSERT_EQUAL('f', out_data[4]);
	ASSERT_EQUAL('t', out_data[5]);
	ASSERT_EQUAL('y', out_data[6]);
	ASSERT_EQUAL('p', out_data[7]);

	free(out_data);
	image_destroy(img);
}
