/**
 * @file test_heif_exif_integration.c
 * @brief Integration test for HEIF decoder with EXIF/XMP metadata
 *
 * Tests that the HEIF decoder properly extracts and stores EXIF/XMP metadata
 * in the image_t structure during decoding.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/image.h"
#include "../ctest.h"

#ifdef HAVE_EXIF_READER
#include "../../imgcat2/metadata/exif_reader.h"
#endif

/* Forward declaration of decode_heif from decoder */
image_t **decode_heif(const uint8_t *data, size_t len, int *frame_count);

/**
 * Helper function to load file into memory
 */
static uint8_t *load_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	uint8_t *data = malloc(fsize);
	if (data == NULL) {
		fclose(f);
		return NULL;
	}

	size_t read = fread(data, 1, fsize, f);
	fclose(f);

	if (read != (size_t)fsize) {
		free(data);
		return NULL;
	}

	*size = fsize;
	return data;
}

/**
 * HEIF test file with EXIF metadata (loaded from disk)
 */
#define HEIF_WITH_EXIF_PATH "../../../src/tests/data/heif/sample_exif.heif"

/**
 * Test that HEIF decoder extracts EXIF metadata
 */
CTEST(heif_exif_integration, decode_with_exif)
{
	/* Load test HEIF with EXIF from file */
	size_t data_size = 0;
	uint8_t *data = load_file(HEIF_WITH_EXIF_PATH, &data_size);
	ASSERT_NOT_NULL(data);

	int frame_count = 0;
	image_t **frames = decode_heif(data, data_size, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	image_t *img = frames[0];

	/* Verify image was decoded */
	ASSERT_NOT_NULL(img->pixels);
	ASSERT_TRUE(img->width > 0);
	ASSERT_TRUE(img->height > 0);

#ifdef HAVE_EXIF_READER
	/* Verify EXIF metadata was extracted */
	ASSERT_NOT_NULL(img->exif);

	/* sample_exif.heif should have camera make/model */
	ASSERT_TRUE(strlen(img->exif->make) > 0);
	ASSERT_TRUE(strlen(img->exif->model) > 0);
#else
	/* If EXIF reader is disabled, metadata should be NULL */
	ASSERT_NULL(img->exif);
	ASSERT_NULL(img->xmp);
#endif

	/* Cleanup */
	image_destroy(img);
	free(frames);
	free(data);
}

/**
 * Test that HEIF without EXIF doesn't crash
 */
CTEST(heif_exif_integration, decode_without_exif)
{
	/* Minimal HEIF without EXIF (if available) */
	const char *path = "../../../src/tests/data/heif/sample_no_exif.heif";
	size_t data_size = 0;
	uint8_t *data = load_file(path, &data_size);

	if (data == NULL) {
		/* Skip test if file doesn't exist */
		CTEST_LOG("Skipping test - sample_no_exif.heif not found");
		return;
	}

	int frame_count = 0;
	image_t **frames = decode_heif(data, data_size, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	image_t *img = frames[0];

	/* Image should be decoded successfully */
	ASSERT_NOT_NULL(img->pixels);
	ASSERT_TRUE(img->width > 0);
	ASSERT_TRUE(img->height > 0);

	/* No EXIF metadata should be present */
	ASSERT_NULL(img->exif);
	ASSERT_NULL(img->xmp);

	/* Cleanup */
	image_destroy(img);
	free(frames);
	free(data);
}

/**
 * Test HEIF with EXIF offset prefix handling
 */
CTEST(heif_exif_integration, decode_with_exif_offset_prefix)
{
	/* Load test HEIF with EXIF from file */
	size_t data_size = 0;
	uint8_t *data = load_file(HEIF_WITH_EXIF_PATH, &data_size);
	ASSERT_NOT_NULL(data);

	int frame_count = 0;
	image_t **frames = decode_heif(data, data_size, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_NOT_NULL(frames[0]);

	image_t *img = frames[0];

#ifdef HAVE_EXIF_READER
	/* Verify EXIF metadata was extracted correctly
	 * This tests that the 4-byte offset prefix is handled properly */
	ASSERT_NOT_NULL(img->exif);

	/* Check that at least make or model is present */
	bool has_metadata = (strlen(img->exif->make) > 0) || (strlen(img->exif->model) > 0);
	ASSERT_TRUE(has_metadata);
#endif

	/* Cleanup */
	image_destroy(img);
	free(frames);
	free(data);
}
