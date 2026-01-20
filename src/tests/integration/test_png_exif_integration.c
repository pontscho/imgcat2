/**
 * @file test_png_exif_integration.c
 * @brief Integration test for PNG decoder with EXIF/XMP metadata
 *
 * Tests that the PNG decoder properly extracts and stores EXIF/XMP metadata
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

/* Forward declaration of decode_png from decoder */
image_t **decode_png(const uint8_t *data, size_t len, int *frame_count);

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
 * PNG test file with EXIF metadata (loaded from disk)
 */
#define PNG_WITH_EXIF_PATH "../src/tests/data/png/sample_exif.png"

/**
 * Test that PNG decoder extracts EXIF metadata from eXIf chunk
 */
CTEST(png_exif_integration, decode_with_exif)
{
	/* Load test PNG with EXIF from file */
	size_t data_size = 0;
	uint8_t *data = load_file(PNG_WITH_EXIF_PATH, &data_size);
	ASSERT_NOT_NULL(data);

	int frame_count = 0;
	image_t **frames = decode_png(data, data_size, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	image_t *img = frames[0];

	/* Verify image was decoded */
	ASSERT_EQUAL(100, img->width);
	ASSERT_EQUAL(100, img->height);
	ASSERT_NOT_NULL(img->pixels);

#ifdef HAVE_EXIF_READER
	/* Verify EXIF metadata was extracted */
	ASSERT_NOT_NULL(img->exif);
	ASSERT_STR("Canon", img->exif->make);
	ASSERT_STR("TestCam", img->exif->model);

	/* XMP should be NULL (no XMP data in this PNG) */
	ASSERT_NULL(img->xmp);
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
 * Test that PNG without EXIF doesn't crash
 */
CTEST(png_exif_integration, decode_without_exif)
{
	/* Load a PNG without EXIF from file */
	size_t data_size = 0;
	uint8_t *data = load_file("../src/tests/data/png/hell.png", &data_size);
	ASSERT_NOT_NULL(data);

	int frame_count = 0;
	image_t **frames = decode_png(data, data_size, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	image_t *img = frames[0];

	/* Image should be decoded successfully */
	ASSERT_EQUAL(512, img->width);
	ASSERT_EQUAL(512, img->height);

	/* EXIF may be present from tEXt chunks, but no camera Make/Model */
#ifdef HAVE_EXIF_READER
	if (img->exif != NULL) {
		/* hell.png has tEXt chunks (Title, Copyright), so EXIF exists */
		/* But it should not have camera Make/Model from eXIf chunk */
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

/**
 * Test that PNG decoder extracts tEXt chunks and maps them to EXIF fields
 */
CTEST(png_exif_integration, decode_with_text_chunks)
{
	/* Load test PNG with tEXt chunks from file */
	size_t data_size = 0;
	uint8_t *data = load_file("../src/tests/data/png/sample_text.png", &data_size);
	ASSERT_NOT_NULL(data);

	int frame_count = 0;
	image_t **frames = decode_png(data, data_size, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	image_t *img = frames[0];

	/* Verify image was decoded */
	ASSERT_EQUAL(50, img->width);
	ASSERT_EQUAL(50, img->height);
	ASSERT_NOT_NULL(img->pixels);

#ifdef HAVE_EXIF_READER
	/* Verify tEXt metadata was extracted and mapped to EXIF fields */
	ASSERT_NOT_NULL(img->exif);
	ASSERT_STR("Test Image Title", img->exif->description);
	ASSERT_STR("Test Author Name", img->exif->artist);
	ASSERT_STR("Copyright 2026 Test", img->exif->copyright);
	ASSERT_STR("This is a test comment", img->exif->user_comment);
	ASSERT_STR("TestSoftware 1.0", img->exif->software);

	/* XMP should be NULL (no XMP data in this PNG) */
	ASSERT_NULL(img->xmp);
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
