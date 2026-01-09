/**
 * @file decoder_qoi.c
 * @brief QOI (Quite OK Image) decoder implementation
 *
 * Decodes QOI images to RGBA8888 format using the QOI header-only library.
 * QOI is a fast, simple, lossless image format.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#include "decoder.h"

/* QOI implementation */
#define QOI_IMPLEMENTATION
#define QOI_NO_STDIO
#include "../../vendor/qoi/qoi.h"

/**
 * @brief Decode QOI image (single frame)
 *
 * Decodes a QOI image to RGBA8888 format.
 * QOI files are always single-frame (no animation support).
 *
 * @param data Raw QOI file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 */
static image_t **decode_qoi_static(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_qoi_static\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Decode QOI image
	qoi_desc desc;
	void *pixels = qoi_decode(data, (int)len, &desc, 4); // 4 = RGBA
	if (pixels == NULL) {
		fprintf(stderr, "Error: Failed to decode QOI image\n");
		return NULL;
	}

	// Create image_t structure
	image_t *output = image_create((uint32_t)desc.width, (uint32_t)desc.height);
	if (output == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		free(pixels);
		return NULL;
	}

	// Copy pixels (QOI already returns RGBA8888, same as our format)
	size_t pixel_size = (size_t)desc.width * (size_t)desc.height * 4;
	memcpy(output->pixels, pixels, pixel_size);

	// Free QOI decoder buffer
	free(pixels);

	// Allocate frames array (single frame)
	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(output);
		return NULL;
	}

	frames[0] = output;
	*frame_count = 1;

	return frames;
}

/**
 * @brief Main entry point for QOI decoding
 *
 * @param data Raw QOI file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (always 1 for QOI)
 * @return Array of image_t* pointers, or NULL on error
 */
image_t **decode_qoi(const uint8_t *data, size_t len, int *frame_count)
{
	// QOI files are always single images (no animation)
	return decode_qoi_static(data, len, frame_count);
}
