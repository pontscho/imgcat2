/**
 * @file test_pipeline_end_to_end.c
 * @brief End-to-end pipeline integration tests
 *
 * Tests complete image processing pipeline: read → decode → scale → render
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
#include "../../imgcat2/decoders/magic.h"
#include "../ctest.h"

/**
 * @test Test calculate_target_dimensions() function
 *
 * Verifies terminal dimension calculation for half-block rendering.
 */
CTEST(integration, target_dimensions)
{
	target_dimensions_t dims;

	/* 80x24 terminal, no offset */
	dims = calculate_target_dimensions(80, 24, 0);
	ASSERT_EQUAL(80, dims.width);
	ASSERT_EQUAL(48, dims.height); /* 24 * 2 for half-blocks */

	/* 100x30 terminal, 2 line offset */
	dims = calculate_target_dimensions(100, 30, 2);
	ASSERT_EQUAL(100, dims.width);
	ASSERT_EQUAL(56, dims.height); /* (30 - 2) * 2 */

	/* Zero dimensions should return {0, 0} */
	dims = calculate_target_dimensions(0, 24, 0);
	ASSERT_EQUAL(0, dims.width);
	ASSERT_EQUAL(0, dims.height);
}

/**
 * @test Test pipeline_decode() with minimal PNG
 *
 * Verifies decoding step of pipeline with embedded PNG data.
 */
CTEST(integration, pipeline_decode_png)
{
	/* Minimal 1x1 PNG */
	static const uint8_t png_data[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		                                0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3A, 0x7E, 0x9B, 0x55, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0x60, 0x00,
		                                0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	/* Initialize decoder registry */
	decoder_registry_init();

	image_t **frames = NULL;
	int frame_count = 0;

	int result = pipeline_decode(png_data, sizeof(png_data), &frames, &frame_count);

	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);

	/* Cleanup */
	decoder_free_frames(frames, frame_count);
}

/**
 * @test Test pipeline_decode() with minimal JPEG
 *
 * Verifies decoding step of pipeline with embedded JPEG data.
 */
CTEST(integration, pipeline_decode_jpeg)
{
	/* Minimal 1x1 JPEG */
	static const uint8_t jpeg_data[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00,
		                                 0x43, 0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08, 0x07, 0x07, 0x07, 0x09, 0x09, 0x08, 0x0A, 0x0C, 0x14, 0x0D, 0x0C, 0x0B, 0x0B, 0x0C,
		                                 0x19, 0x12, 0x13, 0x0F, 0x14, 0x1D, 0x1A, 0x1F, 0x1E, 0x1D, 0x1A, 0x1C, 0x1C, 0x20, 0x24, 0x2E, 0x27, 0x20, 0x22, 0x2C, 0x23, 0x1C, 0x1C,
		                                 0x28, 0x37, 0x29, 0x2C, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1F, 0x27, 0x39, 0x3D, 0x38, 0x32, 0x3C, 0x2E, 0x33, 0x34, 0x32, 0xFF, 0xC0, 0x00,
		                                 0x0B, 0x08, 0x00, 0x01, 0x00, 0x01, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xC4, 0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00, 0xD2, 0xFF, 0xD9 };

	decoder_registry_init();

	image_t **frames = NULL;
	int frame_count = 0;

	int result = pipeline_decode(jpeg_data, sizeof(jpeg_data), &frames, &frame_count);

	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test Test pipeline_decode() with BMP
 *
 * Verifies decoding step of pipeline with embedded BMP data.
 */
CTEST(integration, pipeline_decode_bmp)
{
	/* Minimal 1x1 BMP */
	static const uint8_t bmp_data[] = { 0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x01, 0x00,
		                                0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
		                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	decoder_registry_init();

	image_t **frames = NULL;
	int frame_count = 0;

	int result = pipeline_decode(bmp_data, sizeof(bmp_data), &frames, &frame_count);

	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test Test pipeline_decode() with invalid data
 *
 * Verifies that pipeline_decode() rejects invalid image data.
 */
CTEST(integration, pipeline_decode_invalid)
{
	const uint8_t invalid_data[] = "Not an image file";

	decoder_registry_init();

	image_t **frames = NULL;
	int frame_count = 0;

	int result = pipeline_decode(invalid_data, sizeof(invalid_data), &frames, &frame_count);

	/* Should fail */
	ASSERT_EQUAL(-1, result);
	ASSERT_NULL(frames);
	ASSERT_EQUAL(0, frame_count);
}

/**
 * @test Test pipeline_scale() with simple scaling
 *
 * Verifies scaling step of pipeline.
 */
CTEST(integration, pipeline_scale_basic)
{
	/* Create a 10x10 test image */
	image_t *src = image_create(10, 10);
	ASSERT_NOT_NULL(src);

	/* Create frames array */
	image_t **frames = malloc(sizeof(image_t *));
	frames[0] = src;

	/* Create CLI options with fit mode */
	cli_options_t opts = { 0 };
	opts.fit_mode = true;
	opts.top_offset = 0;

	image_t **scaled = NULL;
	int result = pipeline_scale(frames, 1, &opts, &scaled);

	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(scaled);
	ASSERT_NOT_NULL(scaled[0]);

	/* Scaled image should exist and be different from source */
	ASSERT_TRUE(scaled[0]->width > 0);
	ASSERT_TRUE(scaled[0]->height > 0);

	/* Cleanup */
	image_destroy(scaled[0]);
	free(scaled);
	image_destroy(src);
	free(frames);
}

/**
 * @test Test full pipeline: decode → scale
 *
 * Verifies end-to-end pipeline flow with PNG.
 */
CTEST(integration, full_pipeline_png)
{
	/* Minimal 1x1 PNG */
	static const uint8_t png_data[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		                                0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3A, 0x7E, 0x9B, 0x55, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0x60, 0x00,
		                                0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	decoder_registry_init();

	/* Step 1: Decode */
	image_t **frames = NULL;
	int frame_count = 0;
	int result = pipeline_decode(png_data, sizeof(png_data), &frames, &frame_count);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);

	/* Step 2: Scale */
	cli_options_t opts = { 0 };
	opts.fit_mode = true;
	opts.top_offset = 0;

	image_t **scaled = NULL;
	result = pipeline_scale(frames, frame_count, &opts, &scaled);
	ASSERT_EQUAL(0, result);
	ASSERT_NOT_NULL(scaled);
	ASSERT_NOT_NULL(scaled[0]);

	/* Cleanup */
	image_destroy(scaled[0]);
	free(scaled);
	decoder_free_frames(frames, frame_count);
}

/**
 * @test Test MIME type detection in pipeline
 *
 * Verifies that detect_mime_type() works in pipeline context.
 */
CTEST(integration, mime_detection_pipeline)
{
	/* PNG signature */
	static const uint8_t png_sig[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
	mime_type_t mime = detect_mime_type(png_sig, sizeof(png_sig));
	ASSERT_EQUAL(MIME_PNG, mime);

	/* JPEG signature (needs at least 8 bytes for detect_mime_type) */
	static const uint8_t jpeg_sig[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46 };
	mime = detect_mime_type(jpeg_sig, sizeof(jpeg_sig));
	ASSERT_EQUAL(MIME_JPEG, mime);

	/* BMP signature (needs at least 8 bytes for detect_mime_type) */
	static const uint8_t bmp_sig[] = { 'B', 'M', 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00 };
	mime = detect_mime_type(bmp_sig, sizeof(bmp_sig));
	ASSERT_EQUAL(MIME_BMP, mime);

	/* Unknown */
	static const uint8_t unknown[] = "random data";
	mime = detect_mime_type(unknown, sizeof(unknown));
	ASSERT_EQUAL(MIME_UNKNOWN, mime);
}
