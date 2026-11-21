/**
 * @file decoder_stb.c
 * @brief STB image decoder implementation (fallback for multiple formats)
 *
 * Uses stb_image.h to decode PNG, JPEG, BMP, TGA, PSD, HDR, PNM formats.
 * This is the fallback decoder when native libraries are not available.
 */

#include <stdio.h>
#include <stdlib.h>

#include "decoder.h"

/* Define STB_IMAGE_IMPLEMENTATION once in this file */
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC     /* TU-local: no stbi_* globals leak from libimg2 */
#define STBI_ASSERT(x) ((void)0)
#define STBI_NO_STDIO        /* We provide data buffer directly */
#define STBI_FAILURE_USERMSG /* Use custom error messages */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
/* STB_IMAGE_STATIC lets GCC inline stbi__context use; it then flags io_user_data/buflen of the memory
 * context (only read on the callback path, never taken with STBI_NO_STDIO + *_from_memory) */
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "stb_image.h"
#pragma GCC diagnostic pop
#include "../core/log.h"

/**
 * @brief Decode image using stb_image (fallback decoder)
 *
 * Decodes images in formats: PNG, JPEG, BMP, TGA, PSD, HDR, PNM.
 * This is a generic decoder that works for all STB-supported formats.
 *
 * @param data Raw image file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (static image)
 * @return Array with single img2_image_t*, or NULL on error
 *
 * @note STB only supports static images (no animation)
 * @note Returns RGBA8888 format (4 channels, 8 bits per channel)
 */
static img2_image_t **decode_stb_frames(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_stb");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;

	// Decode with stb_image
	int width, height, channels;
	uint8_t *pixels = stbi_load_from_memory(data, (int)len, &width, &height, &channels, 4);
	if (pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: stb_image failed to decode: %s", stbi_failure_reason());
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Validate dimensions
	if (width <= 0 || height <= 0) {
		img2_log(IMG2_LOG_ERROR, "Error: stb_image returned invalid dimensions: %dx%d", width, height);
		stbi_image_free(pixels);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Create img2_image_t structure
	img2_image_t *img = NULL;
	img2_status_t alloc_st = img2_decode_image_alloc((uint32_t)width, (uint32_t)height, &img);
	if (alloc_st != IMG2_OK) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t structure");
		stbi_image_free(pixels);
		return img2_decode_fail(st, alloc_st);
	}

	// Copy pixel data (stb_image returns RGBA8888 when we request 4 channels)
	size_t pixel_count = (size_t)width * (size_t)height * 4;
	memcpy(img->pixels, pixels, pixel_count);

	// Free stb_image buffer
	stbi_image_free(pixels);

	// Allocate frames array (single frame)
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *));
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		img2_image_destroy(img);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	frames[0] = img;
	*frame_count = 1;

	return frames;
}

img2_status_t img2_decode_stb(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	(void)opts;
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_INTERNAL;
	int count = 0;
	img2_image_t **frames = decode_stb_frames(data, len, &count, &st);
	return img2_frames_adopt(out, frames, count, st);
}
