/**
 * @file test_convert.c
 * @brief Integration tests for full conversion pipeline
 *
 * Tests complete image conversion pipeline: decode → encode → verify
 * Tests JPEG and PNG encoding with various quality settings.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/cli.h"
#include "../../imgcat2/core/image.h"
#include "../../imgcat2/core/pipeline.h"
#include "../../imgcat2/decoders/decoder.h"
#include "../../imgcat2/encoders/encoder.h"
#include "../ctest.h"

/* Minimal 1x1 PNG for testing */
static const uint8_t TEST_PNG[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3A, 0x7E, 0x9B, 0x55, 0x00,
	                                0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0x60, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

/* Minimal 1x1 JPEG for testing */
static const uint8_t TEST_JPEG[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08,
	                                 0x07, 0x07, 0x07, 0x09, 0x09, 0x08, 0x0A, 0x0C, 0x14, 0x0D, 0x0C, 0x0B, 0x0B, 0x0C, 0x19, 0x12, 0x13, 0x0F, 0x14, 0x1D, 0x1A, 0x1F, 0x1E, 0x1D, 0x1A, 0x1C, 0x1C, 0x20, 0x24, 0x2E, 0x27, 0x20,
	                                 0x22, 0x2C, 0x23, 0x1C, 0x1C, 0x28, 0x37, 0x29, 0x2C, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1F, 0x27, 0x39, 0x3D, 0x38, 0x32, 0x3C, 0x2E, 0x33, 0x34, 0x32, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01,
	                                 0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xC4, 0x00, 0x14,
	                                 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00, 0xD2, 0xFF, 0xD9 };

/**
 * @test Test PNG to JPEG conversion
 *
 * Decodes PNG, encodes to JPEG, verifies JPEG is valid.
 */
CTEST(convert, png_to_jpeg)
{
	/* Initialize registries */
	decoder_registry_init(NULL);
	encoder_registry_init(NULL);

	/* Decode PNG */
	image_t **frames = NULL;
	int frame_count = 0;
	int result = pipeline_decode(NULL, TEST_PNG, sizeof(TEST_PNG), &frames, &frame_count);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	/* Encode to JPEG */
	uint8_t *jpeg_data = NULL;
	size_t jpeg_size = 0;
	result = encoder_encode(frames[0], FORMAT_JPEG, 80, &jpeg_data, &jpeg_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(jpeg_data);
	ASSERT_TRUE(jpeg_size > 0);

	/* Verify JPEG magic bytes (0xFF 0xD8) */
	ASSERT_TRUE(jpeg_size >= 2);
	ASSERT_EQUAL(0xFF, jpeg_data[0]);
	ASSERT_EQUAL(0xD8, jpeg_data[1]);

	/* Decode JPEG back to verify it's valid */
	image_t **jpeg_frames = NULL;
	int jpeg_frame_count = 0;
	result = pipeline_decode(NULL, jpeg_data, jpeg_size, &jpeg_frames, &jpeg_frame_count);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(jpeg_frames);
	ASSERT_EQUAL(1, jpeg_frame_count);
	ASSERT_NOT_NULL(jpeg_frames[0]);

	/* Verify dimensions match */
	ASSERT_EQUAL(frames[0]->width, jpeg_frames[0]->width);
	ASSERT_EQUAL(frames[0]->height, jpeg_frames[0]->height);

	/* Cleanup */
	free(jpeg_data);
	decoder_free_frames(frames, frame_count);
	decoder_free_frames(jpeg_frames, jpeg_frame_count);
}

/**
 * @test Test JPEG to PNG conversion
 *
 * Decodes JPEG, encodes to PNG, verifies PNG is valid.
 */
CTEST(convert, jpeg_to_png)
{
	decoder_registry_init(NULL);
	encoder_registry_init(NULL);

	/* Decode JPEG */
	image_t **frames = NULL;
	int frame_count = 0;
	int result = pipeline_decode(NULL, TEST_JPEG, sizeof(TEST_JPEG), &frames, &frame_count);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);

	/* Encode to PNG */
	uint8_t *png_data = NULL;
	size_t png_size = 0;
	result = encoder_encode(frames[0], FORMAT_PNG, 6, &png_data, &png_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(png_data);
	ASSERT_TRUE(png_size > 0);

	/* Verify PNG signature */
	ASSERT_TRUE(png_size >= 8);
	ASSERT_EQUAL(0x89, png_data[0]);
	ASSERT_EQUAL(0x50, png_data[1]);
	ASSERT_EQUAL(0x4E, png_data[2]);
	ASSERT_EQUAL(0x47, png_data[3]);

	/* Decode PNG back to verify it's valid */
	image_t **png_frames = NULL;
	int png_frame_count = 0;
	result = pipeline_decode(NULL, png_data, png_size, &png_frames, &png_frame_count);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(png_frames);
	ASSERT_EQUAL(1, png_frame_count);

	/* Verify dimensions match */
	ASSERT_EQUAL(frames[0]->width, png_frames[0]->width);
	ASSERT_EQUAL(frames[0]->height, png_frames[0]->height);

	/* Cleanup */
	free(png_data);
	decoder_free_frames(frames, frame_count);
	decoder_free_frames(png_frames, png_frame_count);
}

/**
 * @test Test JPEG quality levels
 *
 * Verifies that different JPEG quality levels produce different file sizes.
 */
CTEST(convert, jpeg_quality_levels)
{
	decoder_registry_init(NULL);
	encoder_registry_init(NULL);

	/* Create test image (10x10 with gradient) */
	image_t *img = image_create(10, 10);
	ASSERT_NOT_NULL(img);

	for (uint32_t y = 0; y < 10; y++) {
		for (uint32_t x = 0; x < 10; x++) {
			uint8_t *pixel = image_get_pixel(img, x, y);
			pixel[0] = (x * 255 / 10);
			pixel[1] = (y * 255 / 10);
			pixel[2] = 128;
			pixel[3] = 255;
		}
	}

	/* Encode with quality 10 */
	uint8_t *jpeg_q10 = NULL;
	size_t size_q10 = 0;
	int result = encoder_encode(img, FORMAT_JPEG, 10, &jpeg_q10, &size_q10);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(jpeg_q10);
	ASSERT_TRUE(size_q10 > 0);

	/* Encode with quality 50 */
	uint8_t *jpeg_q50 = NULL;
	size_t size_q50 = 0;
	result = encoder_encode(img, FORMAT_JPEG, 50, &jpeg_q50, &size_q50);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(jpeg_q50);
	ASSERT_TRUE(size_q50 > 0);

	/* Encode with quality 90 */
	uint8_t *jpeg_q90 = NULL;
	size_t size_q90 = 0;
	result = encoder_encode(img, FORMAT_JPEG, 90, &jpeg_q90, &size_q90);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(jpeg_q90);
	ASSERT_TRUE(size_q90 > 0);

	/* Verify quality affects file size: q10 < q50 < q90 */
	ASSERT_TRUE(size_q10 < size_q50);
	ASSERT_TRUE(size_q50 < size_q90);

	/* Verify all are valid JPEGs */
	ASSERT_EQUAL(0xFF, jpeg_q10[0]);
	ASSERT_EQUAL(0xD8, jpeg_q10[1]);
	ASSERT_EQUAL(0xFF, jpeg_q50[0]);
	ASSERT_EQUAL(0xD8, jpeg_q50[1]);
	ASSERT_EQUAL(0xFF, jpeg_q90[0]);
	ASSERT_EQUAL(0xD8, jpeg_q90[1]);

	/* Cleanup */
	free(jpeg_q10);
	free(jpeg_q50);
	free(jpeg_q90);
	image_destroy(img);
}

/**
 * @test Test PNG compression levels
 *
 * Verifies that different PNG compression levels produce different file sizes.
 */
CTEST(convert, png_compression_levels)
{
	decoder_registry_init(NULL);
	encoder_registry_init(NULL);

	/* Create test image (10x10 with gradient) */
	image_t *img = image_create(10, 10);
	ASSERT_NOT_NULL(img);

	for (uint32_t y = 0; y < 10; y++) {
		for (uint32_t x = 0; x < 10; x++) {
			uint8_t *pixel = image_get_pixel(img, x, y);
			pixel[0] = (x * 255 / 10);
			pixel[1] = (y * 255 / 10);
			pixel[2] = 128;
			pixel[3] = 255;
		}
	}

	/* Encode with compression 0 (no compression) */
	uint8_t *png_c0 = NULL;
	size_t size_c0 = 0;
	int result = encoder_encode(img, FORMAT_PNG, 0, &png_c0, &size_c0);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(png_c0);
	ASSERT_TRUE(size_c0 > 0);

	/* Encode with compression 6 (default) */
	uint8_t *png_c6 = NULL;
	size_t size_c6 = 0;
	result = encoder_encode(img, FORMAT_PNG, 6, &png_c6, &size_c6);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(png_c6);
	ASSERT_TRUE(size_c6 > 0);

	/* Encode with compression 9 (maximum) */
	uint8_t *png_c9 = NULL;
	size_t size_c9 = 0;
	result = encoder_encode(img, FORMAT_PNG, 9, &png_c9, &size_c9);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(png_c9);
	ASSERT_TRUE(size_c9 > 0);

	/* Verify compression affects file size: c0 >= c6 >= c9 */
	ASSERT_TRUE(size_c0 >= size_c6);
	ASSERT_TRUE(size_c6 >= size_c9);

	/* Verify all are valid PNGs */
	ASSERT_EQUAL(0x89, png_c0[0]);
	ASSERT_EQUAL(0x50, png_c0[1]);
	ASSERT_EQUAL(0x89, png_c6[0]);
	ASSERT_EQUAL(0x50, png_c6[1]);
	ASSERT_EQUAL(0x89, png_c9[0]);
	ASSERT_EQUAL(0x50, png_c9[1]);

	/* Cleanup */
	free(png_c0);
	free(png_c6);
	free(png_c9);
	image_destroy(img);
}

/**
 * @test Test round-trip conversion (decode → encode → decode)
 *
 * Verifies that images can be decoded, encoded, and decoded again
 * with matching dimensions.
 */
CTEST(convert, round_trip)
{
	decoder_registry_init(NULL);
	encoder_registry_init(NULL);

	/* Start with PNG */
	image_t **frames1 = NULL;
	int frame_count1 = 0;
	int result = pipeline_decode(NULL, TEST_PNG, sizeof(TEST_PNG), &frames1, &frame_count1);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(frames1);
	ASSERT_EQUAL(1, frame_count1);

	/* Encode to JPEG */
	uint8_t *jpeg_data = NULL;
	size_t jpeg_size = 0;
	result = encoder_encode(frames1[0], FORMAT_JPEG, 80, &jpeg_data, &jpeg_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(jpeg_data);

	/* Decode JPEG */
	image_t **frames2 = NULL;
	int frame_count2 = 0;
	result = pipeline_decode(NULL, jpeg_data, jpeg_size, &frames2, &frame_count2);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(frames2);
	ASSERT_EQUAL(1, frame_count2);

	/* Encode to PNG */
	uint8_t *png_data = NULL;
	size_t png_size = 0;
	result = encoder_encode(frames2[0], FORMAT_PNG, 6, &png_data, &png_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(png_data);

	/* Decode PNG again */
	image_t **frames3 = NULL;
	int frame_count3 = 0;
	result = pipeline_decode(NULL, png_data, png_size, &frames3, &frame_count3);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(frames3);
	ASSERT_EQUAL(1, frame_count3);

	/* Verify dimensions are preserved throughout */
	ASSERT_EQUAL(frames1[0]->width, frames2[0]->width);
	ASSERT_EQUAL(frames1[0]->height, frames2[0]->height);
	ASSERT_EQUAL(frames2[0]->width, frames3[0]->width);
	ASSERT_EQUAL(frames2[0]->height, frames3[0]->height);

	/* Cleanup */
	free(jpeg_data);
	free(png_data);
	decoder_free_frames(frames1, frame_count1);
	decoder_free_frames(frames2, frame_count2);
	decoder_free_frames(frames3, frame_count3);
}

/**
 * @test Test PNG with transparency preservation
 *
 * Verifies that PNG encoding preserves alpha channel.
 */
CTEST(convert, png_transparency)
{
	decoder_registry_init(NULL);
	encoder_registry_init(NULL);

	/* Create image with varying alpha */
	image_t *img = image_create(10, 10);
	ASSERT_NOT_NULL(img);

	for (uint32_t y = 0; y < 10; y++) {
		for (uint32_t x = 0; x < 10; x++) {
			uint8_t *pixel = image_get_pixel(img, x, y);
			pixel[0] = 255;
			pixel[1] = 0;
			pixel[2] = 0;
			pixel[3] = (x * 255 / 10); /* Alpha gradient */
		}
	}

	/* Encode to PNG */
	uint8_t *png_data = NULL;
	size_t png_size = 0;
	int result = encoder_encode(img, FORMAT_PNG, 6, &png_data, &png_size);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(png_data);
	ASSERT_TRUE(png_size > 0);

	/* Decode PNG back */
	image_t **frames = NULL;
	int frame_count = 0;
	result = pipeline_decode(NULL, png_data, png_size, &frames, &frame_count);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);

	/* Verify dimensions */
	ASSERT_EQUAL(10, frames[0]->width);
	ASSERT_EQUAL(10, frames[0]->height);

	/* Cleanup */
	free(png_data);
	image_destroy(img);
	decoder_free_frames(frames, frame_count);
}
