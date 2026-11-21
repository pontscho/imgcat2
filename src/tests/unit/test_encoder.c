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

	/* Verify the const table is populated (no init needed) */
	ASSERT_TRUE(img2_encoder_count() > 0);
	ASSERT_NOT_NULL(img2_encoder_info(0));
	ASSERT_NULL(img2_encoder_info(img2_encoder_count()));

	/* Verify at least one encoder is available (JPEG or PNG) */
#if defined(IMG2_HAVE_LIBJPEG) || defined(IMG2_HAVE_LIBPNG)
	ASSERT_TRUE(img2_encoder_count() >= 1);
#endif

#ifdef IMG2_HAVE_LIBJPEG
	/* Verify JPEG encoder exists in registry */
	bool found_jpeg = false;
	for (size_t i = 0; i < img2_encoder_count(); i++) {
		if (img2_encoder_info(i)->format == IMG2_FORMAT_JPEG) {
			found_jpeg = true;
			ASSERT_NOT_NULL(img2_encoder_info(i)->name);
			ASSERT_NOT_NULL(img2_encoder_info(i)->extension);
			ASSERT_NOT_NULL(img2_encoder_find(img2_encoder_info(i)->format)->encode);
			break;
		}
	}
	ASSERT_TRUE(found_jpeg);
#endif

#ifdef IMG2_HAVE_LIBPNG
	/* Verify PNG encoder exists in registry */
	bool found_png = false;
	for (size_t i = 0; i < img2_encoder_count(); i++) {
		if (img2_encoder_info(i)->format == IMG2_FORMAT_PNG) {
			found_png = true;
			ASSERT_NOT_NULL(img2_encoder_info(i)->name);
			ASSERT_NOT_NULL(img2_encoder_info(i)->extension);
			ASSERT_NOT_NULL(img2_encoder_find(img2_encoder_info(i)->format)->encode);
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
	ASSERT_EQUAL(IMG2_FORMAT_JPEG, encoder->info.format);
	ASSERT_NOT_NULL(encoder->info.name);
	ASSERT_NOT_NULL(encoder->info.extension);
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
	ASSERT_EQUAL(IMG2_FORMAT_PNG, encoder->info.format);
	ASSERT_NOT_NULL(encoder->info.name);
	ASSERT_NOT_NULL(encoder->info.extension);
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
	img2_status_t result = img2_encoder_dispatch(NULL, IMG2_FORMAT_JPEG, 80, &out_data, &out_size);
	ASSERT_EQUAL(IMG2_E_ARG, result);
	ASSERT_NULL(out_data);

	/* NULL out_data pointer */
	result = img2_encoder_dispatch(img, IMG2_FORMAT_JPEG, 80, NULL, &out_size);
	ASSERT_EQUAL(IMG2_E_ARG, result);

	/* NULL out_size pointer */
	result = img2_encoder_dispatch(img, IMG2_FORMAT_JPEG, 80, &out_data, NULL);
	ASSERT_EQUAL(IMG2_E_ARG, result);

	/* IMG2_FORMAT_NONE should fail */
	result = img2_encoder_dispatch(img, IMG2_FORMAT_NONE, 80, &out_data, &out_size);
	ASSERT_EQUAL(IMG2_E_ARG, result);
	ASSERT_NULL(out_data);

	img2_image_destroy(img);
}

/**
 * @test The const encoder table is stable across img2_encoder_registry_init() calls
 *
 * The table is const; img2_encoder_registry_init() only logs it.
 */
CTEST(encoder, registry_init_idempotent)
{
	img2_encoder_registry_init();
	size_t first_count = img2_encoder_count();
	const img2_encoder_info_t *first = img2_encoder_info(0);

	img2_encoder_registry_init();
	img2_encoder_registry_init();

	ASSERT_EQUAL(first_count, img2_encoder_count());
	ASSERT_TRUE(first == img2_encoder_info(0));
	ASSERT_TRUE(img2_can_encode(IMG2_FORMAT_NONE) == false);
}

/**
 * @test Unbuilt / invalid formats: silent finder, IMG2_E_UNSUPPORTED from the dispatcher
 */
CTEST(encoder, dispatch_unsupported)
{
	uint8_t *out_data = NULL;
	size_t out_size = 0;
	img2_image_t *img = img2_image_alloc(1, 1);
	ASSERT_NOT_NULL(img);
	ASSERT_EQUAL(IMG2_E_UNSUPPORTED, img2_encoder_dispatch(img, (img2_format_t)99, 80, &out_data, &out_size));
	ASSERT_NULL(out_data);
	img2_image_destroy(img);
}
