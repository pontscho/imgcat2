/**
 * @file test_tiff_exif_integration.c
 * @brief Integration test for TIFF decoder with EXIF metadata
 *
 * Tests that the TIFF decoder properly extracts and stores EXIF metadata
 * in the image_t structure during decoding. TIFF files ARE EXIF format
 * (TIFF = Tagged Image File Format, EXIF uses TIFF structure).
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

/* Forward declaration of decode_tiff from decoder */
image_t **decode_tiff(const uint8_t *data, size_t len, int *frame_count);

/**
 * Read entire file into memory
 */
static uint8_t *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	*len = ftell(f);
	fseek(f, 0, SEEK_SET);

	uint8_t *data = malloc(*len);
	if (!data) {
		fclose(f);
		return NULL;
	}

	size_t read = fread(data, 1, *len, f);
	fclose(f);

	if (read != *len) {
		free(data);
		return NULL;
	}

	return data;
}

/**
 * Test that TIFF decoder extracts EXIF metadata from real TIFF file
 */
CTEST(tiff_exif_integration, decode_with_exif)
{
	size_t len = 0;
	uint8_t *data = read_file("src/tests/data/tiff/Rudless.tiff", &len);

	ASSERT_NOT_NULL(data);
	ASSERT_TRUE(len > 0);

	int frame_count = 0;
	image_t **frames = decode_tiff(data, len, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	image_t *img = frames[0];

	/* Verify image was decoded */
	ASSERT_TRUE(img->width > 0);
	ASSERT_TRUE(img->height > 0);
	ASSERT_NOT_NULL(img->pixels);

#ifdef HAVE_EXIF_READER
	/* Verify EXIF metadata was extracted (TIFF IS EXIF format) */
	ASSERT_NOT_NULL(img->exif);

	/* TIFF files typically have basic metadata */
	/* Just verify that exif structure is populated, don't check specific values */
	/* as they depend on the test file content */
#else
	/* If EXIF reader is disabled, metadata should be NULL */
	ASSERT_NULL(img->exif);
#endif

	/* Cleanup */
	image_destroy(img);
	free(frames);
	free(data);
}

/**
 * Test that TIFF decoder handles another TIFF file
 */
CTEST(tiff_exif_integration, decode_another_tiff)
{
	size_t len = 0;
	uint8_t *data = read_file("src/tests/data/tiff/BSG1.tiff", &len);

	ASSERT_NOT_NULL(data);
	ASSERT_TRUE(len > 0);

	int frame_count = 0;
	image_t **frames = decode_tiff(data, len, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	image_t *img = frames[0];

	/* Verify image was decoded */
	ASSERT_TRUE(img->width > 0);
	ASSERT_TRUE(img->height > 0);
	ASSERT_NOT_NULL(img->pixels);

#ifdef HAVE_EXIF_READER
	/* EXIF structure should be present (TIFF IS EXIF) */
	ASSERT_NOT_NULL(img->exif);
#else
	/* If EXIF reader is disabled, metadata should be NULL */
	ASSERT_NULL(img->exif);
#endif

	/* Cleanup */
	image_destroy(img);
	free(frames);
	free(data);
}
