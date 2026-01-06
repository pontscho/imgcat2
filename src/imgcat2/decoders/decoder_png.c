/**
 * @file decoder_png.c
 * @brief PNG decoder implementation using libpng
 *
 * Decodes PNG images (RGB, RGBA, indexed, interlaced) to RGBA8888 format.
 * Handles transparency, gamma correction, and various PNG types.
 */

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decoder.h"

/**
 * @brief Decode PNG image using libpng
 *
 * Uses the simplified libpng API (png_image) for easier decoding.
 * Automatically handles:
 * - Color type conversion (RGB, RGBA, indexed, grayscale)
 * - Interlaced PNGs (Adam7)
 * - Transparency (tRNS chunk)
 * - Gamma correction
 *
 * @param data Raw PNG file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (PNG is static)
 * @return Array with single image_t*, or NULL on error
 *
 * @note PNG does not support animation (use APNG for that, not implemented here)
 * @note Output format is always RGBA8888
 */
image_t **decode_png(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_png\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Initialize png_image structure
	png_image image;
	memset(&image, 0, sizeof(png_image));
	image.version = PNG_IMAGE_VERSION;

	// Begin reading from memory buffer
	if (png_image_begin_read_from_memory(&image, data, len) == 0) {
		fprintf(stderr, "Error: libpng failed to read PNG header: %s\n", image.message);
		png_image_free(&image);
		return NULL;
	}

	// Force RGBA8888 output format (8 bits per channel, 4 channels)
	image.format = PNG_FORMAT_RGBA;

	// Calculate required buffer size
	size_t buffer_size = PNG_IMAGE_SIZE(image);
	if (buffer_size == 0) {
		fprintf(stderr, "Error: libpng returned zero buffer size\n");
		png_image_free(&image);
		return NULL;
	}

	// Allocate temporary buffer for decoded pixels
	uint8_t *buffer = (uint8_t *)malloc(buffer_size);
	if (buffer == NULL) {
		fprintf(stderr, "Error: Failed to allocate PNG decode buffer (%zu bytes)\n", buffer_size);
		png_image_free(&image);
		return NULL;
	}

	// Finish reading and decode
	// Parameters:
	// - image: png_image structure
	// - NULL: no background color
	// - buffer: output buffer
	// - 0: row stride (0 = automatic)
	// - NULL: no colormap
	if (png_image_finish_read(&image, NULL, buffer, 0, NULL) == 0) {
		fprintf(stderr, "Error: libpng failed to decode PNG: %s\n", image.message);
		free(buffer);
		png_image_free(&image);
		return NULL;
	}

	// Create image_t structure
	image_t *img = image_create(image.width, image.height);
	if (img == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		free(buffer);
		png_image_free(&image);
		return NULL;
	}

	// Copy pixel data to image_t
	size_t pixel_count = (size_t)image.width * (size_t)image.height * 4;
	memcpy(img->pixels, buffer, pixel_count);

	// Free temporary buffer
	free(buffer);

	// Cleanup png_image structure
	png_image_free(&image);

	// Allocate frames array (single frame for PNG)
	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(img);
		return NULL;
	}

	frames[0] = img;
	*frame_count = 1;

	fprintf(stderr, "PNG decoded: %ux%u, %u-bit RGBA\n", image.width, image.height, PNG_IMAGE_SAMPLE_SIZE(image.format) * 8);

	return frames;
}
