/**
 * @file test_webp_exif_integration.c
 * @brief Integration test for WebP decoder with EXIF/XMP metadata
 *
 * Tests that the WebP decoder properly extracts and stores EXIF/XMP metadata
 * in the img2_image_t structure during decoding.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/core/image.h"
#include "../ctest.h"

#ifdef IMG2_HAVE_EXIF_READER
#include "../../libimg2/metadata/exif_reader.h"
#include "../decoder_internal.h"
#endif

/* Forward declaration of img2_decode_webp from decoder */

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
#ifndef IMG2_TEST_SRC_DIR
#define IMG2_TEST_SRC_DIR "../src/tests"
#endif
#define WEBP_WITH_EXIF_PATH IMG2_TEST_SRC_DIR "/data/webp/sample_exif.webp"

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
	img2_image_t **frames = test_decode(img2_decode_webp, data, data_size, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	img2_image_t *img = frames[0];

	/* Verify image was decoded */
	ASSERT_NOT_NULL(img->pixels);
	ASSERT_TRUE(img->width > 0);
	ASSERT_TRUE(img->height > 0);

#ifdef IMG2_HAVE_EXIF_READER
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
	img2_image_destroy(img);
	free(frames);
	free(data);
}

/**
 * Test that WebP decoder handles another WebP file with EXIF
 */
CTEST(webp_exif_integration, decode_without_exif)
{
	/* Load another WebP with EXIF from file */
	size_t data_size = 0;
	uint8_t *data = load_file(IMG2_TEST_SRC_DIR "/data/webp/example.webp", &data_size);
	ASSERT_NOT_NULL(data);

	int frame_count = 0;
	img2_image_t **frames = test_decode(img2_decode_webp, data, data_size, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	img2_image_t *img = frames[0];

	/* Image should be decoded successfully */
	ASSERT_NOT_NULL(img->pixels);
	ASSERT_TRUE(img->width > 0);
	ASSERT_TRUE(img->height > 0);

	/* example.webp has EXIF metadata (Samsung Galaxy Nexus) */
#ifdef IMG2_HAVE_EXIF_READER
	/* EXIF should be present and populated */
	ASSERT_NOT_NULL(img->exif);
	ASSERT_TRUE(strlen(img->exif->make) > 0);
#else
	ASSERT_NULL(img->exif);
#endif

	/* XMP should be NULL (no XMP data) */
	ASSERT_NULL(img->xmp);

	/* Cleanup */
	img2_image_destroy(img);
	free(frames);
	free(data);
}
