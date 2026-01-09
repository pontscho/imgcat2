/**
 * @file decoder_raw.c
 * @brief RAW image decoder implementation using libraw
 *
 * Decodes camera RAW images (CR2, NEF, ARW, DNG, RAF, ORF, RW2, etc.) to RGBA8888 format.
 * Supports 100+ RAW formats via LibRAW library.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libraw/libraw.h>
/* clang-format on */

#include "decoder.h"

/**
 * @brief Decode static RAW image (single frame)
 *
 * Decodes a RAW camera image to RGBA8888 format.
 * RAW files are always single-frame (no animation support).
 *
 * @param data Raw RAW file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 * @note Processing parameters: sRGB color space, camera white balance, AHD demosaicing
 */
static image_t **decode_raw_static(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_raw_static\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Create LibRAW processor
	libraw_data_t *raw = libraw_init(0);
	if (raw == NULL) {
		fprintf(stderr, "Error: Failed to initialize LibRAW\n");
		return NULL;
	}

	// Open buffer
	// Cast away const - libraw API doesn't accept const but won't modify data
	int ret = libraw_open_buffer(raw, (void *)data, len);
	if (ret != LIBRAW_SUCCESS) {
		fprintf(stderr, "Error: Failed to open RAW buffer: %s\n", libraw_strerror(ret));
		libraw_close(raw);
		return NULL;
	}

	// Unpack RAW data
	ret = libraw_unpack(raw);
	if (ret != LIBRAW_SUCCESS) {
		fprintf(stderr, "Error: Failed to unpack RAW data: %s\n", libraw_strerror(ret));
		libraw_close(raw);
		return NULL;
	}

	// Configure processing parameters
	raw->params.output_color = 1; // sRGB color space
	raw->params.output_bps = 8; // 8-bit output
	raw->params.gamm[0] = 1.0 / 2.4; // sRGB gamma curve
	raw->params.gamm[1] = 12.92; // sRGB gamma slope
	raw->params.no_auto_bright = 0; // Enable auto brightness
	raw->params.use_camera_wb = 1; // Use camera white balance
	raw->params.use_auto_wb = 0; // Disable auto white balance
	raw->params.user_qual = 3; // AHD demosaicing
	raw->params.use_fuji_rotate = 1; // Rotate Fuji images correctly

	// Process RAW data (demosaicing, color correction)
	ret = libraw_dcraw_process(raw);
	if (ret != LIBRAW_SUCCESS) {
		fprintf(stderr, "Error: Failed to process RAW data: %s\n", libraw_strerror(ret));
		libraw_close(raw);
		return NULL;
	}

	// Get processed image
	libraw_processed_image_t *processed = libraw_dcraw_make_mem_image(raw, &ret);
	if (processed == NULL) {
		fprintf(stderr, "Error: Failed to create memory image: %s\n", libraw_strerror(ret));
		libraw_close(raw);
		return NULL;
	}

	// Create image_t structure
	image_t *output = image_create(processed->width, processed->height);
	if (output == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		libraw_dcraw_clear_mem(processed);
		libraw_close(raw);
		return NULL;
	}

	// Convert RGB to RGBA8888
	// LibRAW returns RGB (3 bytes/pixel), we need RGBA8888 (4 bytes/pixel)
	const uint8_t *src = processed->data;
	uint8_t *dst = output->pixels;

	for (int y = 0; y < (int)processed->height; y++) {
		for (int x = 0; x < (int)processed->width; x++) {
			*dst++ = *src++; // R
			*dst++ = *src++; // G
			*dst++ = *src++; // B
			*dst++ = 0xFF; // A (fully opaque)
		}
	}

	// Cleanup LibRAW resources
	libraw_dcraw_clear_mem(processed);
	libraw_close(raw);

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
 * @brief Main entry point for RAW decoding
 *
 * @param data Raw RAW file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (always 1 for RAW)
 * @return Array of image_t* pointers, or NULL on error
 */
image_t **decode_raw(const uint8_t *data, size_t len, int *frame_count)
{
	// RAW files are always single images (no animation)
	return decode_raw_static(data, len, frame_count);
}
