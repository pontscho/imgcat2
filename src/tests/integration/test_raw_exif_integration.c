/**
 * @file test_raw_exif_integration.c
 * @brief Integration test for RAW decoder with EXIF metadata
 *
 * Tests that the RAW decoder (libraw) properly extracts and stores EXIF metadata
 * in the image_t structure during decoding.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../imgcat2/core/image.h"
#include "../../imgcat2/core/pipeline.h"
#include "../ctest.h"

#ifdef HAVE_EXIF_READER
#include "../../imgcat2/metadata/exif_reader.h"
#endif

/* Forward declaration of decode_raw from decoder */
image_t **decode_raw(const uint8_t *data, size_t len, int *frame_count);

/**
 * Helper function to load test file
 */
static uint8_t *load_test_file(const char *filename, size_t *out_size)
{
	/* Construct path relative to test binary location
	 * Test runs from build/src/tests/, so we need ../../../src/tests/data/raw/ */
	char path[512];
	snprintf(path, sizeof(path), "../../../src/tests/data/raw/%s", filename);

	uint8_t *data = NULL;
	if (!read_file_secure(path, &data, out_size)) {
		fprintf(stderr, "Failed to load test file: %s\n", path);
		return NULL;
	}

	return data;
}

/**
 * Test DNG (Canon) RAW file with EXIF metadata
 */
CTEST(raw_exif_integration, decode_dng_canon)
{
	size_t file_size = 0;
	uint8_t *data = load_test_file("canon_eos_350d.dng", &file_size);
	ASSERT_NOT_NULL(data);

	int frame_count = 0;
	image_t **frames = decode_raw(data, file_size, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	image_t *img = frames[0];

	/* Verify image was decoded */
	ASSERT_TRUE(img->width > 0);
	ASSERT_TRUE(img->height > 0);
	ASSERT_NOT_NULL(img->pixels);

#ifdef HAVE_EXIF_READER
	/* Verify EXIF metadata was extracted */
	ASSERT_NOT_NULL(img->exif);
	ASSERT_STR("Canon", img->exif->make);
	ASSERT_STR("EOS 350D DIGITAL", img->exif->model);
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
 * Test NEF (Nikon) RAW file with EXIF metadata
 */
CTEST(raw_exif_integration, decode_nef_nikon)
{
	size_t file_size = 0;
	uint8_t *data = load_test_file("nikon_d3.nef", &file_size);
	ASSERT_NOT_NULL(data);

	int frame_count = 0;
	image_t **frames = decode_raw(data, file_size, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	image_t *img = frames[0];

	/* Verify image was decoded */
	ASSERT_TRUE(img->width > 0);
	ASSERT_TRUE(img->height > 0);
	ASSERT_NOT_NULL(img->pixels);

#ifdef HAVE_EXIF_READER
	/* Verify EXIF metadata was extracted */
	ASSERT_NOT_NULL(img->exif);
	ASSERT_STR("Nikon", img->exif->make);
	ASSERT_STR("D3", img->exif->model);
	ASSERT_STR("Capture NX 2.0.0 M", img->exif->software);
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
 * Test ARW (Sony) RAW file with EXIF and lens metadata
 */
CTEST(raw_exif_integration, decode_arw_sony)
{
	size_t file_size = 0;
	uint8_t *data = load_test_file("sony_a7m2.arw", &file_size);
	ASSERT_NOT_NULL(data);

	int frame_count = 0;
	image_t **frames = decode_raw(data, file_size, &frame_count);

	ASSERT_NOT_NULL(frames);
	ASSERT_EQUAL(1, frame_count);
	ASSERT_NOT_NULL(frames[0]);

	image_t *img = frames[0];

	/* Verify image was decoded */
	ASSERT_TRUE(img->width > 0);
	ASSERT_TRUE(img->height > 0);
	ASSERT_NOT_NULL(img->pixels);

#ifdef HAVE_EXIF_READER
	/* Verify EXIF metadata was extracted */
	ASSERT_NOT_NULL(img->exif);
	ASSERT_STR("Sony", img->exif->make);
	ASSERT_STR("ILCE-7M2", img->exif->model);
	ASSERT_STR("ILCE-7M2 v1.20", img->exif->software);

	/* Verify lens metadata */
	ASSERT_STR("FE 28-70mm F3.5-5.6 OSS", img->exif->lens_model);
	ASSERT_DBL_NEAR(3.5, img->exif->max_aperture);
#else
	/* If EXIF reader is disabled, metadata should be NULL */
	ASSERT_NULL(img->exif);
#endif

	/* Cleanup */
	image_destroy(img);
	free(frames);
	free(data);
}
