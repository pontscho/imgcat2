/**
 * @file test_webp_exif_integration.c
 * @brief Integration test for WebP decoder with EXIF/XMP metadata
 *
 * Tests that the WebP decoder properly extracts and stores EXIF/XMP metadata
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

/* Forward declaration of decode_webp from decoder */
image_t **decode_webp(const uint8_t *data, size_t len, int *frame_count);

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
 * WebP test file with EXIF metadata (loaded from disk)
 */
#define WEBP_WITH_EXIF_PATH "../src/tests/data/webp/sample_exif.webp"

/**
 * Test that WebP decoder extracts EXIF metadata
 */
CTEST(webp_exif_integration, decode_with_exif)
{
	/* Load test WebP with EXIF from file */
	size_t data_size = 0;
	uint8_t *data = load_file(WEBP_WITH_EXIF_PATH, &data_size);
	ASSERT_NOT_NULL(data);

	int frame_count = 0;
	image_t **frames = decode_webp(data, data_size, &frame_count);

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

	/* sample_exif.webp should have camera make/model */
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
 * Test that WebP without EXIF doesn't crash
 */
CTEST(webp_exif_integration, decode_without_exif)
{
	/* Load a WebP without EXIF from file */
	size_t data_size = 0;
	uint8_t *data = load_file("../src/tests/data/webp/example.webp", &data_size);
	ASSERT_NOT_NULL(data);

	int frame_count = 0;
	image_t **frames = decode_webp(data, data_size, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	image_t *img = frames[0];

	/* Image should be decoded successfully */
	ASSERT_NOT_NULL(img->pixels);
	ASSERT_TRUE(img->width > 0);
	ASSERT_TRUE(img->height > 0);

	/* No EXIF metadata should be present */
#ifdef HAVE_EXIF_READER
	/* example.webp should not have EXIF metadata */
	if (img->exif != NULL) {
		/* If EXIF struct exists, it should be empty */
		ASSERT_TRUE(strlen(img->exif->make) == 0);
		ASSERT_TRUE(strlen(img->exif->model) == 0);
	}
#else
	ASSERT_NULL(img->exif);
#endif

	/* XMP should be NULL (no XMP data) */
	ASSERT_NULL(img->xmp);

	/* Cleanup */
	image_destroy(img);
	free(frames);
	free(data);
}
