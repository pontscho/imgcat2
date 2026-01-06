/**
 * @file decoder_stb.c
 * @brief STB image decoder implementation (fallback for multiple formats)
 *
 * Uses stb_image.h to decode PNG, JPEG, BMP, TGA, PSD, HDR, PNM formats.
 * This is the fallback decoder when native libraries are not available.
 */

#include "decoder.h"
#include <stdio.h>
#include <stdlib.h>

/* Define STB_IMAGE_IMPLEMENTATION once in this file */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO  /* We provide data buffer directly */
#define STBI_FAILURE_USERMSG  /* Use custom error messages */
#include "stb_image.h"

/**
 * @brief Decode image using stb_image (fallback decoder)
 *
 * Decodes images in formats: PNG, JPEG, BMP, TGA, PSD, HDR, PNM.
 * This is a generic decoder that works for all STB-supported formats.
 *
 * @param data Raw image file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (static image)
 * @return Array with single image_t*, or NULL on error
 *
 * @note STB only supports static images (no animation)
 * @note Returns RGBA8888 format (4 channels, 8 bits per channel)
 */
image_t** decode_stb(const uint8_t* data, size_t len, int* frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_stb\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Decode with stb_image
	int width, height, channels;
	uint8_t* pixels = stbi_load_from_memory(data, (int)len, &width, &height, &channels, 4);
	if (pixels == NULL) {
		fprintf(stderr, "Error: stb_image failed to decode: %s\n", stbi_failure_reason());
		return NULL;
	}

	// Validate dimensions
	if (width <= 0 || height <= 0) {
		fprintf(stderr, "Error: stb_image returned invalid dimensions: %dx%d\n", width, height);
		stbi_image_free(pixels);
		return NULL;
	}

	// Create image_t structure
	image_t* img = image_create((uint32_t)width, (uint32_t)height);
	if (img == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		stbi_image_free(pixels);
		return NULL;
	}

	// Copy pixel data (stb_image returns RGBA8888 when we request 4 channels)
	size_t pixel_count = (size_t)width * (size_t)height * 4;
	memcpy(img->pixels, pixels, pixel_count);

	// Free stb_image buffer
	stbi_image_free(pixels);

	// Allocate frames array (single frame)
	image_t** frames = (image_t**)malloc(sizeof(image_t*));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(img);
		return NULL;
	}

	frames[0] = img;
	*frame_count = 1;

	return frames;
}
