/**
 * @file test_decoder_stb.c
 * @brief Unit tests for STB image decoder
 *
 * Tests STB decoder functionality using ctest.h framework.
 * Tests various formats: BMP, TGA, PSD, HDR, PNM (PPM/PGM).
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
 * @test Test decode_stb() with NULL parameters
 *
 * Verifies that decode_stb() rejects NULL input gracefully.
 */
CTEST(decoder_stb, null_parameters)
{
	int frame_count;
	uint8_t dummy_data[10] = { 0 };

	/* NULL data pointer */
	image_t **frames = decode_stb(NULL, 100, &frame_count);
	ASSERT_NULL(frames);

	/* Zero length */
	frames = decode_stb(dummy_data, 0, &frame_count);
	ASSERT_NULL(frames);

	/* NULL frame_count */
	frames = decode_stb(dummy_data, 10, NULL);
	ASSERT_NULL(frames);
}

/**
 * @test Test decode_stb() with invalid data
 *
 * Verifies that decode_stb() rejects invalid image data.
 */
CTEST(decoder_stb, invalid_data)
{
	int frame_count;
	uint8_t invalid_data[] = "This is not a valid image file";

	image_t **frames = decode_stb(invalid_data, sizeof(invalid_data), &frame_count);
	ASSERT_NULL(frames);
	ASSERT_EQUAL(0, frame_count);
}

/**
 * @test Test decode_stb() with minimal 1x1 BMP
 *
 * Tests BMP decoding with a minimal 1x1 24-bit BMP.
 */
CTEST(decoder_stb, decode_bmp_1x1)
{
	/* Minimal 1x1 24-bit BMP (black pixel) */
	static const uint8_t bmp_1x1[] = { 0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, /* BM header */
		                               0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00, /* DIB header */
		                               0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, /* 1x1 dimensions */
		                               0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, /* 24-bit color */
		                               0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* Pixel data (BGR + padding) */
		                               0x00, 0x00 };

	int frame_count;
	image_t **frames = decode_stb(bmp_1x1, sizeof(bmp_1x1), &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);
	ASSERT_NOT_NULL(frames[0]->pixels);

	/* BMP should be converted to RGBA */
	uint8_t *pixel = image_get_pixel(frames[0], 0, 0);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(255, pixel[3]); /* Alpha should be opaque */

	decoder_free_frames(frames, frame_count);
}

/**
 * @test Test decode_stb() with 2x2 BMP
 *
 * Tests BMP decoding with a small 2x2 24-bit BMP.
 */
CTEST(decoder_stb, decode_bmp_2x2)
{
	/* Minimal 2x2 24-bit BMP */
	static const uint8_t bmp_2x2[] = {
		0x42,
		0x4D,
		0x46,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x36,
		0x00,
		0x00,
		0x00,
		0x28,
		0x00,
		0x00,
		0x00,
		0x02,
		0x00,
		0x00,
		0x00,
		0x02,
		0x00, /* 2x2 dimensions */
		0x00,
		0x00,
		0x01,
		0x00,
		0x18,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x10,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		0x00,
		/* Pixel data (BGR format, bottom-up) */
		0xFF,
		0x00,
		0x00,
		0x00,
		0xFF,
		0x00,
		0x00,
		0x00, /* Row 2 (bottom) */
		0x00,
		0x00,
		0xFF,
		0xFF,
		0xFF,
		0xFF,
		0x00,
		0x00 /* Row 1 (top) */
	};

	int frame_count;
	image_t **frames = decode_stb(bmp_2x2, sizeof(bmp_2x2), &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(2, frames[0]->width);
	ASSERT_EQUAL(2, frames[0]->height);

	/* Verify all 4 pixels exist and have opaque alpha */
	for (uint32_t y = 0; y < 2; y++) {
		for (uint32_t x = 0; x < 2; x++) {
			uint8_t *pixel = image_get_pixel(frames[0], x, y);
			ASSERT_NOT_NULL(pixel);
			ASSERT_EQUAL(255, pixel[3]); /* Alpha=255 */
		}
	}

	decoder_free_frames(frames, frame_count);
}

/**
 * @test Test decode_stb() with minimal TGA
 *
 * Tests TGA (Targa) format decoding.
 */
CTEST(decoder_stb, decode_tga)
{
	/* Minimal 1x1 uncompressed 24-bit TGA (black pixel) */
	static const uint8_t tga_1x1[] = {
		0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, /* TGA header */
		0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, /* 1x1 dimensions */
		0x18, 0x00, 0x00, 0x00, 0x00 /* 24-bit + pixel data (BGR) */
	};

	int frame_count;
	image_t **frames = decode_stb(tga_1x1, sizeof(tga_1x1), &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);

	uint8_t *pixel = image_get_pixel(frames[0], 0, 0);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(255, pixel[3]); /* Alpha=255 */

	decoder_free_frames(frames, frame_count);
}

/**
 * @test Test decode_stb() with PPM (Portable Pixmap)
 *
 * Tests PPM format (PNM family) decoding.
 */
CTEST(decoder_stb, decode_ppm)
{
	/* 2x2 PPM P6 (binary format) */
	/* Header: "P6\n2 2\n255\n" followed by 12 bytes of RGB data */
	static const uint8_t ppm_2x2[] = {
		'P',  '6',  '\n', '2', ' ', '2', '\n', '2', '5', '5', '\n', 0xFF, 0x00, 0x00, /* Red */
		0x00, 0xFF, 0x00, /* Green */
		0x00, 0x00, 0xFF, /* Blue */
		0xFF, 0xFF, 0xFF /* White */
	};

	int frame_count;
	image_t **frames = decode_stb(ppm_2x2, sizeof(ppm_2x2), &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(2, frames[0]->width);
	ASSERT_EQUAL(2, frames[0]->height);

	/* Verify all pixels have opaque alpha */
	for (uint32_t y = 0; y < 2; y++) {
		for (uint32_t x = 0; x < 2; x++) {
			uint8_t *pixel = image_get_pixel(frames[0], x, y);
			ASSERT_NOT_NULL(pixel);
			ASSERT_EQUAL(255, pixel[3]); /* Alpha=255 */
		}
	}

	decoder_free_frames(frames, frame_count);
}

/**
 * @test Test decode_stb() with PGM (Portable Graymap)
 *
 * Tests PGM format (PNM family) decoding.
 */
CTEST(decoder_stb, decode_pgm)
{
	/* 2x2 PGM P5 (binary grayscale) */
	static const uint8_t pgm_2x2[] = "P5\n"
	                                 "2 2\n"
	                                 "255\n"
	                                 "\x00\x7F\xAF\xFF"; /* 4 grayscale pixels */

	int frame_count;
	image_t **frames = decode_stb((const uint8_t *)pgm_2x2, strlen((const char *)pgm_2x2) + 4, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(2, frames[0]->width);
	ASSERT_EQUAL(2, frames[0]->height);

	/* Verify grayscale was converted to RGBA */
	for (uint32_t y = 0; y < 2; y++) {
		for (uint32_t x = 0; x < 2; x++) {
			uint8_t *pixel = image_get_pixel(frames[0], x, y);
			ASSERT_NOT_NULL(pixel);
			/* Grayscale means R=G=B */
			ASSERT_EQUAL(pixel[0], pixel[1]);
			ASSERT_EQUAL(pixel[1], pixel[2]);
			ASSERT_EQUAL(255, pixel[3]); /* Alpha=255 */
		}
	}

	decoder_free_frames(frames, frame_count);
}

/**
 * @test Test decoder_free_frames() with NULL
 *
 * Verifies that decoder_free_frames() handles NULL gracefully.
 */
CTEST(decoder_stb, free_frames_null)
{
	/* Should not crash */
	decoder_free_frames(NULL, 0);
	decoder_free_frames(NULL, 10);
}

/**
 * @test Test STB decoder through decoder registry (BMP)
 *
 * Verifies that BMP can be decoded through the decoder_decode() API.
 */
CTEST(decoder_stb, decode_bmp_through_registry)
{
	decoder_registry_init(NULL);

	static const uint8_t bmp_1x1[] = { 0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18,
		                               0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	int frame_count;
	image_t **frames = decoder_decode(NULL, bmp_1x1, sizeof(bmp_1x1), MIME_BMP, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);

	decoder_free_frames(frames, frame_count);
}

/**
 * @test Test decoder_find_by_mime() for BMP
 *
 * Verifies that BMP decoder can be found in registry.
 */
CTEST(decoder_stb, find_by_mime_bmp)
{
	decoder_registry_init(NULL);

	const decoder_t *decoder = decoder_find_by_mime(MIME_BMP);
	ASSERT_NOT_NULL(decoder);
	ASSERT_EQUAL(MIME_BMP, decoder->mime_type);
	ASSERT_NOT_NULL(decoder->name);
	ASSERT_NOT_NULL(decoder->decode);
}

/**
 * @test Test that STB decoder always returns 1 frame
 *
 * STB does not support animation, should always return 1 frame.
 */
CTEST(decoder_stb, single_frame_only)
{
	static const uint8_t bmp_1x1[] = { 0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18,
		                               0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	int frame_count = -1;
	image_t **frames = decode_stb(bmp_1x1, sizeof(bmp_1x1), &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count); /* STB is always single-frame */

	decoder_free_frames(frames, frame_count);
}
