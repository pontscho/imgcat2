/**
 * @file test_encoder.c
 * @brief Unit tests for encoder registry
 *
 * Tests encoder registry initialization, lookup, and validation functionality
 * using ctest.h framework.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../libimg2/core/image.h"
#include "../../libimg2/encoders/encoder.h"
#include "../ctest.h"

/**
 * @test Test img2_encoder_registry_init()
 *
 * Verifies that encoder registry initializes correctly and populates
 * the global registry with available encoders.
 */
CTEST(encoder, registry_init)
{
	/* Initialize registry */
	img2_encoder_registry_init();

	/* Verify registry is populated */
	ASSERT_NOT_NULL(img2_encoder_registry);
	ASSERT_TRUE(img2_encoder_registry_size > 0);

	/* Verify at least one encoder is available (JPEG or PNG) */
#if defined(IMG2_HAVE_LIBJPEG) || defined(IMG2_HAVE_LIBPNG)
	ASSERT_TRUE(img2_encoder_registry_size >= 1);
#endif

#ifdef IMG2_HAVE_LIBJPEG
	/* Verify JPEG encoder exists in registry */
	bool found_jpeg = false;
	for (size_t i = 0; i < img2_encoder_registry_size; i++) {
		if (img2_encoder_registry[i].format == IMG2_FORMAT_JPEG) {
			found_jpeg = true;
			ASSERT_NOT_NULL(img2_encoder_registry[i].name);
			ASSERT_NOT_NULL(img2_encoder_registry[i].extension);
			ASSERT_NOT_NULL(img2_encoder_registry[i].encode);
			break;
		}
	}
	ASSERT_TRUE(found_jpeg);
#endif

#ifdef IMG2_HAVE_LIBPNG
	/* Verify PNG encoder exists in registry */
	bool found_png = false;
	for (size_t i = 0; i < img2_encoder_registry_size; i++) {
		if (img2_encoder_registry[i].format == IMG2_FORMAT_PNG) {
			found_png = true;
			ASSERT_NOT_NULL(img2_encoder_registry[i].name);
			ASSERT_NOT_NULL(img2_encoder_registry[i].extension);
			ASSERT_NOT_NULL(img2_encoder_registry[i].encode);
			break;
		}
	}
	ASSERT_TRUE(found_png);
#endif
}

/**
 * @test Test img2_encoder_find() for JPEG
 *
 * Verifies that JPEG encoder can be found by format.
 */
CTEST(encoder, find_by_format_jpeg)
{
	img2_encoder_registry_init();

#ifdef IMG2_HAVE_LIBJPEG
	const img2_encoder_entry_t *encoder = img2_encoder_find(IMG2_FORMAT_JPEG);
	ASSERT_NOT_NULL(encoder);
	ASSERT_EQUAL(IMG2_FORMAT_JPEG, encoder->format);
	ASSERT_NOT_NULL(encoder->name);
	ASSERT_NOT_NULL(encoder->extension);
	ASSERT_NOT_NULL(encoder->encode);
#else
	/* If JPEG not available, should return NULL */
	const img2_encoder_entry_t *encoder = img2_encoder_find(IMG2_FORMAT_JPEG);
	ASSERT_NULL(encoder);
#endif
}

/**
 * @test Test img2_encoder_find() for PNG
 *
 * Verifies that PNG encoder can be found by format.
 */
CTEST(encoder, find_by_format_png)
{
	img2_encoder_registry_init();

#ifdef IMG2_HAVE_LIBPNG
	const img2_encoder_entry_t *encoder = img2_encoder_find(IMG2_FORMAT_PNG);
	ASSERT_NOT_NULL(encoder);
	ASSERT_EQUAL(IMG2_FORMAT_PNG, encoder->format);
	ASSERT_NOT_NULL(encoder->name);
	ASSERT_NOT_NULL(encoder->extension);
	ASSERT_NOT_NULL(encoder->encode);
#else
	/* If PNG not available, should return NULL */
	const img2_encoder_entry_t *encoder = img2_encoder_find(IMG2_FORMAT_PNG);
	ASSERT_NULL(encoder);
#endif
}

/**
 * @test Test img2_encoder_find() with IMG2_FORMAT_NONE
 *
 * Verifies that encoder lookup returns NULL for IMG2_FORMAT_NONE.
 */
CTEST(encoder, find_by_format_none)
{
	img2_encoder_registry_init();

	/* IMG2_FORMAT_NONE should always return NULL */
	const img2_encoder_entry_t *encoder = img2_encoder_find(IMG2_FORMAT_NONE);
	ASSERT_NULL(encoder);
}

/**
 * @test Test img2_encoder_dispatch() with NULL parameters
 *
 * Verifies that img2_encoder_dispatch() validates NULL inputs correctly.
 */
CTEST(encoder, encode_null_checks)
{
	img2_encoder_registry_init();

	uint8_t *out_data = NULL;
	size_t out_size = 0;

	/* Create minimal valid image */
	img2_image_t *img = img2_image_alloc(1, 1);
	ASSERT_NOT_NULL(img);

	/* NULL image pointer */
	int result = img2_encoder_dispatch(NULL, IMG2_FORMAT_JPEG, 80, &out_data, &out_size);
	ASSERT_EQUAL(-1, result);
	ASSERT_NULL(out_data);

	/* NULL out_data pointer */
	result = img2_encoder_dispatch(img, IMG2_FORMAT_JPEG, 80, NULL, &out_size);
	ASSERT_EQUAL(-1, result);

	/* NULL out_size pointer */
	result = img2_encoder_dispatch(img, IMG2_FORMAT_JPEG, 80, &out_data, NULL);
	ASSERT_EQUAL(-1, result);

	/* IMG2_FORMAT_NONE should fail */
	result = img2_encoder_dispatch(img, IMG2_FORMAT_NONE, 80, &out_data, &out_size);
	ASSERT_EQUAL(-1, result);
	ASSERT_NULL(out_data);

	img2_image_destroy(img);
}

/**
 * @test Test img2_encoder_registry_init() is idempotent
 *
 * Verifies that calling img2_encoder_registry_init() multiple times is safe.
 */
CTEST(encoder, registry_init_idempotent)
{
	/* Initialize multiple times */
	img2_encoder_registry_init();
	const img2_encoder_entry_t *first_registry = img2_encoder_registry;
	size_t first_count = img2_encoder_registry_size;

	img2_encoder_registry_init();
	const img2_encoder_entry_t *second_registry = img2_encoder_registry;
	size_t second_count = img2_encoder_registry_size;

	img2_encoder_registry_init();
	const img2_encoder_entry_t *third_registry = img2_encoder_registry;
	size_t third_count = img2_encoder_registry_size;

	/* All pointers and counts should be the same */
	ASSERT_TRUE(first_registry == second_registry);
	ASSERT_TRUE(second_registry == third_registry);
	ASSERT_EQUAL(first_count, second_count);
	ASSERT_EQUAL(second_count, third_count);
}
