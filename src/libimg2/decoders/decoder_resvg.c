/**
 * @file decoder_resvg.c
 * @brief SVG decoder using resvg library
 *
 * SVG vector image decoding to RGBA8888 using resvg library.
 * Provides superior SVG spec compliance compared to nanosvg.
 */

#include <math.h>
#include <resvg/resvg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decoder.h"
#include "../core/log.h"

/**
 * @brief Decode SVG image using resvg (single frame)
 *
 * Decodes an SVG image to RGBA8888 format using the resvg library.
 * Provides better SVG specification compliance than nanosvg, with support
 * for complex gradients, filters, and transformations.
 *
 * @param data Raw SVG file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single img2_image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 * @note Falls back to nanosvg if resvg fails
 */
static img2_image_t **decode_svg_resvg_frames(const uint8_t *data, size_t len, int *frame_count, img2_status_t *st)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decode_svg_resvg");
		return img2_decode_fail(st, IMG2_E_ARG);
	}

	// Initialize output
	*frame_count = 0;

	// Create resvg options
	resvg_options *opt = resvg_options_create();
	if (opt == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create resvg options");
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Parse SVG from memory
	resvg_render_tree *tree = NULL;
	resvg_error err = resvg_parse_tree_from_data((const char *)data, len, opt, &tree);

	if (err != RESVG_OK || tree == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: resvg failed to parse SVG (error code: %d)", err);
		resvg_options_destroy(opt);
		return img2_decode_fail(st, IMG2_E_CODEC);
	}

	// Get SVG dimensions
	resvg_size size = resvg_get_image_size(tree);
	/* [P17] float -> uint32_t is undefined for NaN, negative and >= 2^32 sizes: <= 0 / NaN become 0 (FORMAT
	 * below), anything above IMG2_MAX_DIMENSION is clamped to IMG2_MAX_DIMENSION + 1 (LIMIT below) */
	uint32_t width = size.width > 0.0f ? (uint32_t)fminf(size.width, (float)IMG2_MAX_DIMENSION + 1.0f) : 0;
	uint32_t height = size.height > 0.0f ? (uint32_t)fminf(size.height, (float)IMG2_MAX_DIMENSION + 1.0f) : 0;

	// Validate dimensions
	if (width == 0 || height == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid SVG dimensions: %ux%u", width, height);
		resvg_tree_destroy(tree);
		resvg_options_destroy(opt);
		return img2_decode_fail(st, IMG2_E_FORMAT);
	}

	if (width > IMG2_MAX_DIMENSION || height > IMG2_MAX_DIMENSION) {
		img2_log(IMG2_LOG_ERROR, "Error: SVG dimensions exceed maximum (%u): %ux%u", IMG2_MAX_DIMENSION, width, height);
		resvg_tree_destroy(tree);
		resvg_options_destroy(opt);
		return img2_decode_fail(st, IMG2_E_LIMIT);
	}

	// Check pixel count limit
	uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
	if (pixel_count > IMG2_MAX_PIXELS) {
		img2_log(IMG2_LOG_ERROR, "Error: SVG pixel count exceeds maximum (%lu): %llu", (unsigned long)IMG2_MAX_PIXELS, (unsigned long long)pixel_count);
		resvg_tree_destroy(tree);
		resvg_options_destroy(opt);
		return img2_decode_fail(st, IMG2_E_LIMIT);
	}

	// Create img2_image_t structure
	img2_image_t *output = img2_image_alloc(width, height);
	if (output == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create image_t structure");
		resvg_tree_destroy(tree);
		resvg_options_destroy(opt);
		return img2_decode_fail(st, IMG2_E_NOMEM);
	}

	// Render SVG to pixel buffer
	resvg_transform transform = resvg_transform_identity();
	resvg_render(tree, transform, width, height, (char *)output->pixels);

	// Cleanup resvg resources
	resvg_tree_destroy(tree);
	resvg_options_destroy(opt);

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

img2_status_t img2_decode_svg_resvg(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
	(void)opts;
	if (out == NULL) {
		return IMG2_E_ARG;
	}
	img2_status_t st = IMG2_E_INTERNAL;
	int count = 0;
	img2_image_t **frames = decode_svg_resvg_frames(data, len, &count, &st);
	return img2_frames_adopt(out, frames, count, st);
}
