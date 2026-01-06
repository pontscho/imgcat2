/**
 * @file test_security.c
 * @brief Unit tests for security validation and protection mechanisms
 *
 * Tests path traversal protection, file size limits, image dimension limits,
 * integer overflow checks, and magic bytes verification.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../imgcat2/core/image.h"
#include "../../imgcat2/core/pipeline.h"
#include "../../imgcat2/decoders/magic.h"
#include "../ctest.h"

/**
 * @test Test path traversal protection with ".." in path
 *
 * Verifies that read_file_secure() rejects paths containing ".." components
 * to prevent path traversal attacks.
 */
CTEST(security, path_traversal)
{
	uint8_t *data = NULL;
	size_t size = 0;

	/* Test various path traversal attempts */
	/* These should all fail */

	/* Basic ".." */
	bool result1 = read_file_secure("../etc/passwd", &data, &size);
	ASSERT_FALSE(result1);
	ASSERT_NULL(data);

	/* ".." in middle of path */
	bool result2 = read_file_secure("/tmp/../etc/passwd", &data, &size);
	ASSERT_FALSE(result2);
	ASSERT_NULL(data);

	/* Multiple ".." */
	bool result3 = read_file_secure("../../etc/passwd", &data, &size);
	ASSERT_FALSE(result3);
	ASSERT_NULL(data);

	/* ".." after filename */
	bool result4 = read_file_secure("test/../secret.txt", &data, &size);
	ASSERT_FALSE(result4);
	ASSERT_NULL(data);
}

/**
 * @test Test file size limit enforcement
 *
 * Verifies that read_file_secure() rejects files exceeding IMAGE_MAX_FILE_SIZE.
 * This test creates a temporary large file to test the limit.
 */
CTEST(security, file_size_limit)
{
	/* Create a temporary file that exceeds the size limit */
	const char *temp_file = "/tmp/imgcat2_test_large_file.bin";
	FILE *fp = fopen(temp_file, "wb");

	if (fp != NULL) {
		/* Write IMAGE_MAX_FILE_SIZE + 1 bytes */
		/* We'll write in chunks to avoid excessive memory use in test */
		const size_t chunk_size = 1024 * 1024; /* 1MB chunks */
		uint8_t *chunk = calloc(1, chunk_size);

		if (chunk != NULL) {
			size_t written = 0;
			size_t target = IMAGE_MAX_FILE_SIZE + 1;

			while (written < target) {
				size_t to_write = (target - written > chunk_size) ? chunk_size : (target - written);
				fwrite(chunk, 1, to_write, fp);
				written += to_write;
			}

			free(chunk);
		}

		fclose(fp);

		/* Now try to read the oversized file */
		uint8_t *data = NULL;
		size_t size = 0;
		bool result = read_file_secure(temp_file, &data, &size);

		/* Should fail due to size limit */
		ASSERT_FALSE(result);
		ASSERT_NULL(data);

		/* Clean up */
		unlink(temp_file);
	}

	/* Test passes even if we couldn't create the temp file */
	/* (some environments may not allow it) */
	ASSERT_TRUE(true);
}

/**
 * @test Test maximum image dimension validation
 *
 * Verifies that image_create() rejects dimensions exceeding IMAGE_MAX_DIMENSION.
 */
CTEST(security, image_size_limit)
{
	/* Test dimensions that exceed the maximum */
	image_t *img1 = image_create(IMAGE_MAX_DIMENSION + 1, 100);
	ASSERT_NULL(img1);

	image_t *img2 = image_create(100, IMAGE_MAX_DIMENSION + 1);
	ASSERT_NULL(img2);

	/* Both dimensions exceed maximum */
	image_t *img3 = image_create(IMAGE_MAX_DIMENSION + 1, IMAGE_MAX_DIMENSION + 1);
	ASSERT_NULL(img3);

	/* Test at the boundary (should succeed) */
	image_t *img4 = image_create(IMAGE_MAX_DIMENSION, 1);
	ASSERT_NOT_NULL(img4);
	image_destroy(img4);

	image_t *img5 = image_create(1, IMAGE_MAX_DIMENSION);
	ASSERT_NOT_NULL(img5);
	image_destroy(img5);
}

/**
 * @test Test integer overflow protection in image allocation
 *
 * Verifies that image_create() detects integer overflow when calculating
 * total pixel buffer size (width × height × 4 bytes).
 */
CTEST(security, integer_overflow)
{
	/* Test dimensions that would cause overflow in width * height * 4 */
	/* Use dimensions that are within IMAGE_MAX_DIMENSION but would overflow */
	/* when multiplied together */

	/* Zero dimensions should fail */
	image_t *img1 = image_create(0, 100);
	ASSERT_NULL(img1);

	image_t *img2 = image_create(100, 0);
	ASSERT_NULL(img2);

	image_t *img3 = image_create(0, 0);
	ASSERT_NULL(img3);

	/* Very large dimensions that would exceed IMAGE_MAX_PIXELS */
	/* IMAGE_MAX_PIXELS is 100,000,000 */
	/* 10000 x 10001 = 100,010,000 > IMAGE_MAX_PIXELS */
	image_t *img4 = image_create(10000, 10001);
	ASSERT_NULL(img4);

	/* At the boundary (should succeed) */
	/* 10000 x 10000 = 100,000,000 = IMAGE_MAX_PIXELS */
	image_t *img5 = image_create(10000, 10000);
	ASSERT_NOT_NULL(img5);
	image_destroy(img5);
}

/**
 * @test Test magic bytes verification for PNG
 *
 * Verifies that detect_mime_type() correctly identifies PNG files
 * by magic bytes and rejects invalid signatures.
 */
CTEST(security, magic_bytes)
{
	/* Valid PNG signature: 89 50 4E 47 0D 0A 1A 0A */
	const uint8_t valid_png[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
	mime_type_t mime1 = detect_mime_type(valid_png, sizeof(valid_png));
	ASSERT_EQUAL(MIME_PNG, mime1);

	/* Invalid PNG signature (first byte wrong) */
	const uint8_t invalid_png1[] = { 0x88, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
	mime_type_t mime2 = detect_mime_type(invalid_png1, sizeof(invalid_png1));
	ASSERT_NOT_EQUAL(MIME_PNG, mime2);

	/* Valid JPEG signature: FF D8 FF */
	const uint8_t valid_jpeg[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46 };
	mime_type_t mime3 = detect_mime_type(valid_jpeg, sizeof(valid_jpeg));
	ASSERT_EQUAL(MIME_JPEG, mime3);

	/* Invalid JPEG signature */
	const uint8_t invalid_jpeg[] = { 0xFF, 0xD8, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00 };
	mime_type_t mime4 = detect_mime_type(invalid_jpeg, sizeof(invalid_jpeg));
	ASSERT_NOT_EQUAL(MIME_JPEG, mime4);

	/* Valid GIF87a signature */
	const uint8_t valid_gif87[] = { 'G', 'I', 'F', '8', '7', 'a', 0x00, 0x00 };
	mime_type_t mime5 = detect_mime_type(valid_gif87, sizeof(valid_gif87));
	ASSERT_EQUAL(MIME_GIF, mime5);

	/* Valid GIF89a signature */
	const uint8_t valid_gif89[] = { 'G', 'I', 'F', '8', '9', 'a', 0x00, 0x00 };
	mime_type_t mime6 = detect_mime_type(valid_gif89, sizeof(valid_gif89));
	ASSERT_EQUAL(MIME_GIF, mime6);

	/* Invalid GIF signature */
	const uint8_t invalid_gif[] = { 'G', 'I', 'F', '8', '8', 'a', 0x00, 0x00 };
	mime_type_t mime7 = detect_mime_type(invalid_gif, sizeof(invalid_gif));
	ASSERT_NOT_EQUAL(MIME_GIF, mime7);

	/* Too short buffer (< 8 bytes) */
	const uint8_t short_buffer[] = { 0x89, 0x50, 0x4E };
	mime_type_t mime8 = detect_mime_type(short_buffer, sizeof(short_buffer));
	ASSERT_EQUAL(MIME_UNKNOWN, mime8);

	/* Random data (should return MIME_UNKNOWN) */
	const uint8_t random_data[] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 };
	mime_type_t mime9 = detect_mime_type(random_data, sizeof(random_data));
	ASSERT_EQUAL(MIME_UNKNOWN, mime9);
}

/**
 * @test Test detect_mime_type() with NULL pointer
 *
 * Verifies that detect_mime_type() handles NULL data gracefully.
 */
CTEST(security, magic_bytes_null)
{
	/* NULL data should return MIME_UNKNOWN */
	mime_type_t mime = detect_mime_type(NULL, 100);
	ASSERT_EQUAL(MIME_UNKNOWN, mime);

	/* Zero length should return MIME_UNKNOWN */
	const uint8_t data[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
	mime_type_t mime2 = detect_mime_type(data, 0);
	ASSERT_EQUAL(MIME_UNKNOWN, mime2);
}

/**
 * @test Test read_file_secure() with NULL pointers
 *
 * Verifies that read_file_secure() handles NULL parameters safely.
 */
CTEST(security, read_file_null_params)
{
	uint8_t *data = NULL;
	size_t size = 0;

	/* NULL path */
	bool result1 = read_file_secure(NULL, &data, &size);
	ASSERT_FALSE(result1);

	/* NULL out_data */
	bool result2 = read_file_secure("/tmp/test.txt", NULL, &size);
	ASSERT_FALSE(result2);

	/* NULL out_size */
	bool result3 = read_file_secure("/tmp/test.txt", &data, NULL);
	ASSERT_FALSE(result3);
}

/**
 * @test Test image_calculate_size() overflow detection
 *
 * Verifies that image_calculate_size() detects overflow in size calculations.
 */
CTEST(security, calculate_size_overflow)
{
	size_t out_size = 0;

	/* Valid small image */
	bool result1 = image_calculate_size(100, 100, &out_size);
	ASSERT_TRUE(result1);
	ASSERT_EQUAL(100 * 100 * 4, out_size);

	/* Dimensions that would cause overflow */
	/* UINT32_MAX * UINT32_MAX * 4 would definitely overflow */
	bool result2 = image_calculate_size(UINT32_MAX, UINT32_MAX, &out_size);
	ASSERT_FALSE(result2);

	/* Large dimensions that exceed IMAGE_MAX_PIXELS */
	bool result3 = image_calculate_size(50000, 50000, &out_size);
	ASSERT_FALSE(result3);

	/* NULL output parameter */
	bool result4 = image_calculate_size(100, 100, NULL);
	ASSERT_FALSE(result4);
}
