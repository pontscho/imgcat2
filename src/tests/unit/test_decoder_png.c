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

#include "../../libimg2/core/image.h"
#include "../../libimg2/decoders/decoder.h"
#include "../ctest.h"
#include "../decoder_internal.h"

/**
 * @brief Directory of the PNG fixtures, relative to the build directory (test working directory)
 */
#define PNG_FIXTURE_DIR "../src/tests/data/png/"

/**
 * @brief Load a PNG fixture file into memory
 *
 * @param name File name inside PNG_FIXTURE_DIR
 * @param size Output: file size in bytes
 * @return Malloc'd file contents, or NULL on error
 */
static uint8_t *load_fixture(const char *name, size_t *size)
{
	char path[512];
	snprintf(path, sizeof(path), "%s%s", PNG_FIXTURE_DIR, name);

	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	uint8_t *data = (fsize > 0) ? malloc((size_t)fsize) : NULL;
	if (data == NULL || fread(data, 1, (size_t)fsize, f) != (size_t)fsize) {
		free(data);
		fclose(f);
		return NULL;
	}

	fclose(f);
	*size = (size_t)fsize;
	return data;
}

/**
 * @brief Decode a PNG fixture file
 *
 * @param name File name inside PNG_FIXTURE_DIR
 * @param frame_count Output: number of decoded frames
 * @return Decoded frames, or NULL on error
 */
static img2_image_t **decode_fixture(const char *name, int *frame_count)
{
	size_t size = 0;
	uint8_t *data = load_fixture(name, &size);
	if (data == NULL) {
		return NULL;
	}

	img2_image_t **frames = test_decode(img2_decode_png, data, size, frame_count);
	free(data);
	return frames;
}

/**
 * @brief Assert an RGBA pixel value
 *
 * @param img Decoded image
 * @param x Pixel column
 * @param y Pixel row
 * @param r Expected red
 * @param g Expected green
 * @param b Expected blue
 * @param a Expected alpha
 */
static void assert_pixel(img2_image_t *img, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	uint8_t *pixel = img2_image_get_pixel(img, x, y);
	ASSERT_NOT_NULL(pixel);
	ASSERT_EQUAL(r, pixel[0]);
	ASSERT_EQUAL(g, pixel[1]);
	ASSERT_EQUAL(b, pixel[2]);
	ASSERT_EQUAL(a, pixel[3]);
}

/**
 * @test Test img2_decode_png() with NULL parameters
 *
 * Verifies that img2_decode_png() rejects NULL input gracefully.
 */
CTEST(decoder_png, null_parameters)
{
	int frame_count;
	uint8_t dummy_data[10] = { 0 };

	/* NULL data pointer */
	img2_image_t **frames = test_decode(img2_decode_png, NULL, 100, &frame_count);
	ASSERT_NULL(frames);

	/* Zero length */
	frames = test_decode(img2_decode_png, dummy_data, 0, &frame_count);
	ASSERT_NULL(frames);

	/* NULL frame_count */
	frames = test_decode(img2_decode_png, dummy_data, 10, NULL);
	ASSERT_NULL(frames);
}

/**
 * @test Test img2_decode_png() with invalid PNG data
 *
 * Verifies that img2_decode_png() rejects invalid data.
 */
CTEST(decoder_png, invalid_data)
{
	int frame_count;
	uint8_t invalid_data[] = "This is not a PNG file";

	img2_image_t **frames = test_decode(img2_decode_png, invalid_data, sizeof(invalid_data), &frame_count);
	ASSERT_NULL(frames);
	ASSERT_EQUAL(0, frame_count);
}

/**
 * @test Test img2_decode_png() with minimal valid 1x1 PNG
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
	img2_image_t **frames = test_decode(img2_decode_png, png_1x1_gray, sizeof(png_1x1_gray), &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);
	ASSERT_NOT_NULL(frames[0]->pixels);

	/* Verify RGBA format (should be converted from grayscale) */
	uint8_t *pixel = img2_image_get_pixel(frames[0], 0, 0);
	ASSERT_NOT_NULL(pixel);
	/* Grayscale 0 should convert to R=0, G=0, B=0, A=255 */
	ASSERT_EQUAL(0, pixel[0]); /* R */
	ASSERT_EQUAL(0, pixel[1]); /* G */
	ASSERT_EQUAL(0, pixel[2]); /* B */
	ASSERT_EQUAL(255, pixel[3]); /* A */

	img2_decoder_free_frames(frames, frame_count);
}

/**
 * @test Test img2_decode_png() with repeated 1x1 decoding
 *
 * Tests that multiple PNG decodes work correctly.
 */
CTEST(decoder_png, decode_multiple_times)
{
	/* 1x1 grayscale PNG */
	static const uint8_t png_1x1_gray[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3A, 0x7E, 0x9B, 0x55, 0x00,
		                                    0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0x60, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	/* Decode multiple times to ensure no state issues */
	for (int i = 0; i < 3; i++) {
		int frame_count;
		img2_image_t **frames = test_decode(img2_decode_png, png_1x1_gray, sizeof(png_1x1_gray), &frame_count);

		ASSERT_NOT_NULL(frames);
		ASSERT_EQUAL(1, frame_count);
		ASSERT_NOT_NULL(frames[0]);
		ASSERT_EQUAL(1, frames[0]->width);
		ASSERT_EQUAL(1, frames[0]->height);
		ASSERT_NOT_NULL(frames[0]->pixels);

		img2_decoder_free_frames(frames, frame_count);
	}
}

/**
 * @test Test img2_decoder_free_frames() with NULL
 *
 * Verifies that img2_decoder_free_frames() handles NULL gracefully.
 */
CTEST(decoder_png, free_frames_null)
{
	/* Should not crash */
	img2_decoder_free_frames(NULL, 0);
	img2_decoder_free_frames(NULL, 10);
}

/**
 * @test Test PNG decoder through decoder registry
 *
 * Verifies that PNG can be decoded through the img2_decoder_dispatch() API.
 */
CTEST(decoder_png, decode_through_registry)
{
	/* Initialize decoder registry */
	img2_decoder_registry_init();

	/* 1x1 PNG */
	static const uint8_t png_1x1_gray[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x00, 0x3A, 0x7E, 0x9B, 0x55, 0x00,
		                                    0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0x60, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0xE2, 0x21, 0xBC, 0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82 };

	int frame_count;
	img2_image_t **frames = test_dispatch(NULL, png_1x1_gray, sizeof(png_1x1_gray), IMG2_MIME_PNG, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);
	ASSERT_EQUAL(1, frames[0]->width);
	ASSERT_EQUAL(1, frames[0]->height);

	img2_decoder_free_frames(frames, frame_count);
}

/**
 * @test Test img2_decoder_find() for PNG
 *
 * Verifies that PNG decoder can be found in registry.
 */
CTEST(decoder_png, find_by_mime)
{
	img2_decoder_registry_init();

	const img2_decoder_entry_t *decoder = img2_decoder_find(IMG2_MIME_PNG);
	ASSERT_NOT_NULL(decoder);
	ASSERT_EQUAL(IMG2_MIME_PNG, decoder->info.mime);
	ASSERT_NOT_NULL(decoder->info.name);
	ASSERT_NOT_NULL(decoder->decode);
}

/**
 * @test Interlaced 4-bit palette PNG with tRNS
 *
 * 16x16 Adam7, palette entry i = (i*16, 255-i*16, i*8), tRNS alpha for
 * entries 0..3 = 0, 64, 128, 192 (others opaque), index = (x + y) % 16.
 */
CTEST(decoder_png, palette_trns_interlaced)
{
	int frame_count = 0;
	img2_image_t **frames = decode_fixture("pal4_trns_interlaced.png", &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_EQUAL(16, frames[0]->width);
	ASSERT_EQUAL(16, frames[0]->height);

	for (uint32_t y = 0; y < 16; y++) {
		for (uint32_t x = 0; x < 16; x++) {
			uint32_t i = (x + y) % 16;
			uint8_t alpha = (i < 4) ? (uint8_t)(i * 64) : 255;
			assert_pixel(frames[0], x, y, (uint8_t)(i * 16), (uint8_t)(255 - i * 16), (uint8_t)(i * 8), alpha);
		}
	}

	img2_decoder_free_frames(frames, frame_count);
}

/**
 * @test 4-bit grayscale PNG expands to opaque RGB
 *
 * 16x16, gray value = x (4-bit), so 8-bit output = x * 17.
 */
CTEST(decoder_png, gray_4bit)
{
	int frame_count = 0;
	img2_image_t **frames = decode_fixture("gray4.png", &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);

	for (uint32_t y = 0; y < 16; y++) {
		for (uint32_t x = 0; x < 16; x++) {
			uint8_t v = (uint8_t)(x * 17);
			assert_pixel(frames[0], x, y, v, v, v, 255);
		}
	}

	img2_decoder_free_frames(frames, frame_count);
}

/**
 * @test 16-bit RGB PNG without gAMA
 *
 * Like the libpng simplified API, 16-bit data without colorspace information
 * is treated as linear and encoded to sRGB (not a plain 16->8 strip).
 * Expected values are what png_image_finish_read(PNG_FORMAT_RGBA) returns.
 */
CTEST(decoder_png, rgb_16bit_linear_to_srgb)
{
	int frame_count = 0;
	img2_image_t **frames = decode_fixture("rgb16.png", &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);

	assert_pixel(frames[0], 0, 0, 0, 0, 0, 255);
	assert_pixel(frames[0], 15, 15, 255, 255, 255, 255);
	assert_pixel(frames[0], 8, 4, 192, 140, 168, 255);
	assert_pixel(frames[0], 3, 11, 123, 221, 180, 255);

	img2_decoder_free_frames(frames, frame_count);
}

/**
 * @test Interlaced 16-bit PNG decodes identically to its non-interlaced twin
 *
 * Regression test: libpng's simplified API corrupts interlaced 16-bit images
 * (png_image_read_direct_scaled), the low-level path must not.
 */
CTEST(decoder_png, rgb_16bit_interlaced_matches_progressive)
{
	int count_a = 0;
	int count_b = 0;
	img2_image_t **a = decode_fixture("rgb16.png", &count_a);
	img2_image_t **b = decode_fixture("rgb16_interlaced.png", &count_b);

	ASSERT_NOT_NULL(a);
	ASSERT_NOT_NULL(b);
	ASSERT_EQUAL(a[0]->width, b[0]->width);
	ASSERT_EQUAL(a[0]->height, b[0]->height);
	ASSERT_DATA(a[0]->pixels, (size_t)a[0]->width * a[0]->height * 4, b[0]->pixels, (size_t)b[0]->width * b[0]->height * 4);

	img2_decoder_free_frames(a, count_a);
	img2_decoder_free_frames(b, count_b);
}

/**
 * @test gAMA 1.0 (linear) 8-bit PNG is converted to sRGB; sRGB-tagged PNG is not
 *
 * Both fixtures hold pixel (x, y) = (x*17, y*17, 128).
 */
CTEST(decoder_png, gamma_linear_vs_srgb)
{
	int frame_count = 0;
	img2_image_t **frames = decode_fixture("rgb8_gama100000.png", &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	assert_pixel(frames[0], 0, 0, 0, 0, 186, 255);
	assert_pixel(frames[0], 15, 15, 255, 255, 186, 255);
	assert_pixel(frames[0], 8, 4, 192, 140, 186, 255);
	img2_decoder_free_frames(frames, frame_count);

	frames = decode_fixture("rgb8_srgb.png", &frame_count);
	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	for (uint32_t y = 0; y < 16; y++) {
		for (uint32_t x = 0; x < 16; x++) {
			assert_pixel(frames[0], x, y, (uint8_t)(x * 17), (uint8_t)(y * 17), 128, 255);
		}
	}
	img2_decoder_free_frames(frames, frame_count);
}

/**
 * @test Truncated and corrupted PNGs fail cleanly
 *
 * Every truncation point and an IDAT corruption must return NULL with
 * frame_count 0 (no crash, no partial image).
 */
CTEST(decoder_png, truncated_and_corrupt)
{
	const char *names[] = { "rgb16_interlaced.png", "pal4_trns_interlaced.png", "gray4.png" };

	for (size_t n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
		size_t size = 0;
		uint8_t *data = load_fixture(names[n], &size);
		ASSERT_NOT_NULL(data);

		/* Truncate before IEND's 12 bytes to cut into the image data */
		for (size_t len = 1; len + 12 < size; len += 7) {
			int frame_count = -1;
			img2_image_t **frames = test_decode(img2_decode_png, data, len, &frame_count);
			if (frames != NULL) {
				img2_decoder_free_frames(frames, frame_count);
			}
			ASSERT_NULL(frames);
			ASSERT_EQUAL(0, frame_count);
		}

		/* Corrupt a byte inside the first IDAT payload (CRC mismatch -> error) */
		for (size_t i = 8; i + 12 <= size;) {
			uint32_t chunk_len = ((uint32_t)data[i] << 24) | ((uint32_t)data[i + 1] << 16) | ((uint32_t)data[i + 2] << 8) | data[i + 3];
			if (memcmp(data + i + 4, "IDAT", 4) == 0 && chunk_len > 2) {
				data[i + 8 + chunk_len / 2] ^= 0x5A;
				break;
			}
			i += 12 + chunk_len;
		}

		int frame_count = -1;
		img2_image_t **frames = test_decode(img2_decode_png, data, size, &frame_count);
		ASSERT_NULL(frames);
		ASSERT_EQUAL(0, frame_count);

		free(data);
	}
}
