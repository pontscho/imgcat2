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

#include "../../imgcat2/core/image.h"
#include "../../imgcat2/encoders/encoder.h"
#include "../ctest.h"

/**
 * @test Test encoder_registry_init()
 *
 * Verifies that encoder registry initializes correctly and populates
 * the global registry with available encoders.
 */
CTEST(encoder, registry_init)
{
	/* Initialize registry */
	encoder_registry_init(NULL);

	/* Verify registry is populated */
	ASSERT_NOT_NULL(g_encoder_registry);
	ASSERT_TRUE(g_encoder_count > 0);

	/* Verify at least one encoder is available (JPEG or PNG) */
#if defined(HAVE_LIBJPEG) || defined(HAVE_LIBPNG)
	ASSERT_TRUE(g_encoder_count >= 1);
#endif

#ifdef HAVE_LIBJPEG
	/* Verify JPEG encoder exists in registry */
	bool found_jpeg = false;
	for (size_t i = 0; i < g_encoder_count; i++) {
		if (g_encoder_registry[i].format == FORMAT_JPEG) {
			found_jpeg = true;
			ASSERT_NOT_NULL(g_encoder_registry[i].name);
			ASSERT_NOT_NULL(g_encoder_registry[i].extension);
			ASSERT_NOT_NULL(g_encoder_registry[i].encode);
			break;
		}
	}
	ASSERT_TRUE(found_jpeg);
#endif

#ifdef HAVE_LIBPNG
	/* Verify PNG encoder exists in registry */
	bool found_png = false;
	for (size_t i = 0; i < g_encoder_count; i++) {
		if (g_encoder_registry[i].format == FORMAT_PNG) {
			found_png = true;
			ASSERT_NOT_NULL(g_encoder_registry[i].name);
			ASSERT_NOT_NULL(g_encoder_registry[i].extension);
			ASSERT_NOT_NULL(g_encoder_registry[i].encode);
			break;
		}
	}
	ASSERT_TRUE(found_png);
#endif
}

/**
 * @test Test encoder_find_by_format() for JPEG
 *
 * Verifies that JPEG encoder can be found by format.
 */
CTEST(encoder, find_by_format_jpeg)
{
	encoder_registry_init(NULL);

#ifdef HAVE_LIBJPEG
	const encoder_t *encoder = encoder_find_by_format(FORMAT_JPEG);
	ASSERT_NOT_NULL(encoder);
	ASSERT_EQUAL(FORMAT_JPEG, encoder->format);
	ASSERT_NOT_NULL(encoder->name);
	ASSERT_NOT_NULL(encoder->extension);
	ASSERT_NOT_NULL(encoder->encode);
#else
	/* If JPEG not available, should return NULL */
	const encoder_t *encoder = encoder_find_by_format(FORMAT_JPEG);
	ASSERT_NULL(encoder);
#endif
}

/**
 * @test Test encoder_find_by_format() for PNG
 *
 * Verifies that PNG encoder can be found by format.
 */
CTEST(encoder, find_by_format_png)
{
	encoder_registry_init(NULL);

#ifdef HAVE_LIBPNG
	const encoder_t *encoder = encoder_find_by_format(FORMAT_PNG);
	ASSERT_NOT_NULL(encoder);
	ASSERT_EQUAL(FORMAT_PNG, encoder->format);
	ASSERT_NOT_NULL(encoder->name);
	ASSERT_NOT_NULL(encoder->extension);
	ASSERT_NOT_NULL(encoder->encode);
#else
	/* If PNG not available, should return NULL */
	const encoder_t *encoder = encoder_find_by_format(FORMAT_PNG);
	ASSERT_NULL(encoder);
#endif
}

/**
 * @test Test encoder_find_by_format() with FORMAT_NONE
 *
 * Verifies that encoder lookup returns NULL for FORMAT_NONE.
 */
CTEST(encoder, find_by_format_none)
{
	encoder_registry_init(NULL);

	/* FORMAT_NONE should always return NULL */
	const encoder_t *encoder = encoder_find_by_format(FORMAT_NONE);
	ASSERT_NULL(encoder);
}

/**
 * @test Test encoder_encode() with NULL parameters
 *
 * Verifies that encoder_encode() validates NULL inputs correctly.
 */
CTEST(encoder, encode_null_checks)
{
	encoder_registry_init(NULL);

	uint8_t *out_data = NULL;
	size_t out_size = 0;

	/* Create minimal valid image */
	image_t *img = image_create(1, 1);
	ASSERT_NOT_NULL(img);

	/* NULL image pointer */
	int result = encoder_encode(NULL, FORMAT_JPEG, 80, &out_data, &out_size);
	ASSERT_EQUAL(-1, result);
	ASSERT_NULL(out_data);

	/* NULL out_data pointer */
	result = encoder_encode(img, FORMAT_JPEG, 80, NULL, &out_size);
	ASSERT_EQUAL(-1, result);

	/* NULL out_size pointer */
	result = encoder_encode(img, FORMAT_JPEG, 80, &out_data, NULL);
	ASSERT_EQUAL(-1, result);

	/* FORMAT_NONE should fail */
	result = encoder_encode(img, FORMAT_NONE, 80, &out_data, &out_size);
	ASSERT_EQUAL(-1, result);
	ASSERT_NULL(out_data);

	image_destroy(img);
}

/**
 * @test Test encoder_registry_init() is idempotent
 *
 * Verifies that calling encoder_registry_init() multiple times is safe.
 */
CTEST(encoder, registry_init_idempotent)
{
	/* Initialize multiple times */
	encoder_registry_init(NULL);
	const encoder_t *first_registry = g_encoder_registry;
	size_t first_count = g_encoder_count;

	encoder_registry_init(NULL);
	const encoder_t *second_registry = g_encoder_registry;
	size_t second_count = g_encoder_count;

	encoder_registry_init(NULL);
	const encoder_t *third_registry = g_encoder_registry;
	size_t third_count = g_encoder_count;

	/* All pointers and counts should be the same */
	ASSERT_TRUE(first_registry == second_registry);
	ASSERT_TRUE(second_registry == third_registry);
	ASSERT_EQUAL(first_count, second_count);
	ASSERT_EQUAL(second_count, third_count);
}
