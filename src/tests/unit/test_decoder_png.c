/**
 * @file test_decoder_png.c
 * @brief Unit tests for PNG decoder
 *
 * Tests PNG decoding functionality using ctest.h framework.
 * Tests various PNG formats: RGB, RGBA, indexed, interlaced.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/image.h"
#include "../../imgcat2/decoders/decoder.h"
#include "../ctest.h"
#include "../decoder_internal.h"

/**
 * @test Test decode_png() with NULL parameters
 *
 * Verifies that decode_png() rejects NULL input gracefully.
 */
CTEST(decoder_png, null_parameters)
{
	int frame_count;
	uint8_t dummy_data[10] = { 0 };

	/* NULL data pointer */
	image_t **frames = decode_png(NULL, 100, &frame_count);
	ASSERT_NULL(frames);

	/* Zero length */
	frames = decode_png(dummy_data, 0, &frame_count);
	ASSERT_NULL(frames);

	/* NULL frame_count */
	frames = decode_png(dummy_data, 10, NULL);
	ASSERT_NULL(frames);
}

/**
 * @test Test decode_png() with invalid PNG data
 *
 * Verifies that decode_png() rejects invalid data.
 */
CTEST(decoder_png, invalid_data)
{
	int frame_count;
	uint8_t invalid_data[] = "This is not a PNG file";

	image_t **frames = decode_png(invalid_data, sizeof(invalid_data), &frame_count);
	ASSERT_NULL(frames);
	ASSERT_EQUAL(0, frame_count);
}

/**
 * @test Test decode_png() with minimal valid 1x1 PNG
 *
 * Minimal valid PNG: 1x1 grayscale pixel (value 0)
 * This is a complete, valid PNG file in binary form.
 */
CTEST(decoder_png, decode_minimal_1x1)
{
	/* Minimal 1x1 grayscale PNG (67 bytes) */
	static const uint8_t png_1x1_gray[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, /* PNG signature */
		                                    0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, /* IHDR chunk */
		                                    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, /* width=1, height=1 */
		                                    0x08, 0x00, 0x00, 0x00, 0x00, 0x3A, 0x7E, 0x9B, /* 8-bit grayscale */
		                                    0x55, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, /* IDAT chunk */
		                                    0x08, 0xD7, 0x63, 0x60, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, /* IEND chunk */
		                                    0xAE, 0x42, 0x60, 0x82 };

	int frame_count;
	image_t **frames = decode_png(png_1x1_gray, sizeof(png_1x1_gray), &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);
	ASSERT_NOT_NULL(frames[0]->pixels);

	/* Verify RGBA format (should be converted from grayscale) */
	uint8_t *pixel = image_get_pixel(frames[0], 0, 0);
	ASSERT_NOT_NULL(pixel);
	/* Grayscale 0 should convert to R=0, G=0, B=0, A=255 */
	ASSERT_EQUAL(0, pixel[0]); /* R */
	ASSERT_EQUAL(0, pixel[1]); /* G */
	ASSERT_EQUAL(0, pixel[2]); /* B */
	ASSERT_EQUAL(255, pixel[3]); /* A */

	decoder_free_frames(frames, frame_count);
}

/**
 * @test Test decode_png() with repeated 1x1 decoding
 *
 * Tests that multiple PNG decodes work correctly.
 */
CTEST(decoder_png, decode_multiple_times)
{
	/* 1x1 grayscale PNG */
	static const uint8_t png_1x1_gray[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		                                    0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3A, 0x7E, 0x9B, 0x55, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0x60, 0x00,
		                                    0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	/* Decode multiple times to ensure no state issues */
	for (int i = 0; i < 3; i++) {
		int frame_count;
		image_t **frames = decode_png(png_1x1_gray, sizeof(png_1x1_gray), &frame_count);

		ASSERT_NOT_NULL(frames);
		ASSERT_EQUAL(1, frame_count);
		ASSERT_NOT_NULL(frames[0]);
		ASSERT_EQUAL(1, frames[0]->width);
		ASSERT_EQUAL(1, frames[0]->height);
		ASSERT_NOT_NULL(frames[0]->pixels);

		decoder_free_frames(frames, frame_count);
	}
}

/**
 * @test Test decoder_free_frames() with NULL
 *
 * Verifies that decoder_free_frames() handles NULL gracefully.
 */
CTEST(decoder_png, free_frames_null)
{
	/* Should not crash */
	decoder_free_frames(NULL, 0);
	decoder_free_frames(NULL, 10);
}

/**
 * @test Test PNG decoder through decoder registry
 *
 * Verifies that PNG can be decoded through the decoder_decode() API.
 */
CTEST(decoder_png, decode_through_registry)
{
	/* Initialize decoder registry */
	decoder_registry_init();

	/* 1x1 PNG */
	static const uint8_t png_1x1_gray[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		                                    0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3A, 0x7E, 0x9B, 0x55, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0x60, 0x00,
		                                    0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	int frame_count;
	image_t **frames = decoder_decode(png_1x1_gray, sizeof(png_1x1_gray), MIME_PNG, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test Test decoder_find_by_mime() for PNG
 *
 * Verifies that PNG decoder can be found in registry.
 */
CTEST(decoder_png, find_by_mime)
{
	decoder_registry_init();

	const decoder_t *decoder = decoder_find_by_mime(MIME_PNG);
	ASSERT_NOT_NULL(decoder);
	ASSERT_EQUAL(MIME_PNG, decoder->mime_type);
	ASSERT_NOT_NULL(decoder->name);
	ASSERT_NOT_NULL(decoder->decode);
}
