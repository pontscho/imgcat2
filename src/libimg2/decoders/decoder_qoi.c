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
#define qoi_encode img2_qoi_encode /* qoi.h has no QOIDEF: rename its two extern functions */
#define qoi_decode img2_qoi_decode
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "qoi.h"
#pragma GCC diagnostic pop
#include "../core/log.h"

/**
 * @brief Decode QOI image (single frame)
 *
 * Decodes a QOI image to RGBA8888 format.
 * QOI files are always single-frame (no animation support).
 *
 * @param data Raw QOI file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single img2_image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 */
static img2_image_t **decode_qoi_static(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_qoi_static");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;

	// [P33] qoi_decode() allocates and fills width x height pixels (up to its own 400e6 pixel cap) before the
	// size could be checked: check the header (magic, big-endian width / height) first
	if (len >= QOI_HEADER_SIZE && memcmp(data, "qoif", 4) == 0) {
		const uint32_t w = (uint32_t)data[4] << 24 | (uint32_t)data[5] << 16 | (uint32_t)data[6] << 8 | data[7];
		const uint32_t h = (uint32_t)data[8] << 24 | (uint32_t)data[9] << 16 | (uint32_t)data[10] << 8 | data[11];
		img2_status_t pre_st = img2_decode_check_header_size("QOI", w, h);
		if (pre_st != IMG2_OK) {
			return img2_decode_fail(st, pre_st);
		}
	}

	// Decode QOI image
	qoi_desc desc;
	void *pixels = qoi_decode(data, (int)len, &desc, 4); // 4 = RGBA
	if (pixels == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to decode QOI image");
		return img2_decode_fail(st, IMG2_E_FORMAT);
	}

	// Create img2_image_t structure
	img2_image_t *output = NULL;
	img2_status_t alloc_st = img2_decode_image_alloc((uint32_t)desc.width, (uint32_t)desc.height, &output);
	if (alloc_st != IMG2_OK) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t structure");
		free(pixels);
		return img2_decode_fail(st, alloc_st);
	}

	// Copy pixels (QOI already returns RGBA8888, same as our format)
	size_t pixel_size = (size_t)desc.width * (size_t)desc.height * 4;
	memcpy(output->pixels, pixels, pixel_size);

	// Free QOI decoder buffer
	free(pixels);

	// Allocate frames array (single frame)
	img2_image_t **frames = (img2_image_t **)malloc(sizeof(img2_image_t *));
	if (frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate frames array");
		img2_image_destroy(output);
		return img2_decode_fail(st, IMG2_E_NOMEM);
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
 * @return Array of img2_image_t* pointers, or NULL on error
 */
static img2_image_t **decode_qoi_frames(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	// QOI files are always single images (no animation)
	return decode_qoi_static(data, len, frame_count, st);
}

img2_status_t img2_decode_qoi(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	(void)opts;
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_INTERNAL;
	int count = 0;
	img2_image_t **frames = decode_qoi_frames(data, len, &count, &st);
	return img2_frames_adopt(out, frames, count, st);
}
