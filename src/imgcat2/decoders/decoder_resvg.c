/**
 * @file decoder_resvg.c
 * @brief SVG decoder using resvg library
 *
 * SVG vector image decoding to RGBA8888 using resvg library.
 * Provides superior SVG spec compliance compared to nanosvg.
 */

#include <resvg/resvg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decoder.h"

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
 * @return Array with single image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 * @note Falls back to nanosvg if resvg fails
 */
image_t **decode_svg_resvg(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_svg_resvg\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Create resvg options
	resvg_options *opt = resvg_options_create();
	if (opt == NULL) {
		fprintf(stderr, "Error: Failed to create resvg options\n");
		return NULL;
	}

	// Parse SVG from memory
	resvg_render_tree *tree = NULL;
	resvg_error err = resvg_parse_tree_from_data((const char *)data, len, opt, &tree);

	if (err != RESVG_OK || tree == NULL) {
		fprintf(stderr, "Error: resvg failed to parse SVG (error code: %d)\n", err);
		resvg_options_destroy(opt);
		return NULL;
	}

	// Get SVG dimensions
	resvg_size size = resvg_get_image_size(tree);
	uint32_t width = (uint32_t)size.width;
	uint32_t height = (uint32_t)size.height;

	// Validate dimensions
	if (width == 0 || height == 0) {
		fprintf(stderr, "Error: Invalid SVG dimensions: %ux%u\n", width, height);
		resvg_tree_destroy(tree);
		resvg_options_destroy(opt);
		return NULL;
	}

	if (width > IMAGE_MAX_DIMENSION || height > IMAGE_MAX_DIMENSION) {
		fprintf(stderr, "Error: SVG dimensions exceed maximum (%u): %ux%u\n", IMAGE_MAX_DIMENSION, width, height);
		resvg_tree_destroy(tree);
		resvg_options_destroy(opt);
		return NULL;
	}

	// Check pixel count limit
	uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
	if (pixel_count > IMAGE_MAX_PIXELS) {
		fprintf(stderr, "Error: SVG pixel count exceeds maximum (%lu): %llu\n", (unsigned long)IMAGE_MAX_PIXELS, (unsigned long long)pixel_count);
		resvg_tree_destroy(tree);
		resvg_options_destroy(opt);
		return NULL;
	}

	// Create image_t structure
	image_t *output = image_create(width, height);
	if (output == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		resvg_tree_destroy(tree);
		resvg_options_destroy(opt);
		return NULL;
	}

	// Render SVG to pixel buffer
	resvg_transform transform = resvg_transform_identity();
	resvg_render(tree, transform, width, height, (char *)output->pixels);

	// Cleanup resvg resources
	resvg_tree_destroy(tree);
	resvg_options_destroy(opt);

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
