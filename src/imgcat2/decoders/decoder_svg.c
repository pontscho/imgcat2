/**
 * @file decoder_svg.c
 * @brief SVG (Scalable Vector Graphics) decoder implementation
 *
 * SVG vector image decoding to RGBA8888 using nanosvg library.
 * Rasterizes SVG at native dimensions or 512x512 default.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
/* clang-format on */

#include "decoder.h"

/* NanoSVG implementation */
#define NANOSVG_IMPLEMENTATION
#include "../../vendor/nanosvg/src/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../../vendor/nanosvg/src/nanosvgrast.h"

/**
 * @brief Decode SVG image (single frame)
 *
 * Decodes an SVG image to RGBA8888 format by rasterizing vector graphics.
 * SVG files are always single-frame (no animation support).
 *
 * @param data Raw SVG file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 * @note SVGs without explicit dimensions use 512x512 default
 */
image_t **decode_svg(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_svg\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Create modifiable null-terminated copy for nanosvg parsing
	// nanosvg requires a modifiable buffer (it modifies during parsing)
	char *svg_copy = (char *)malloc(len + 1);
	if (svg_copy == NULL) {
		fprintf(stderr, "Error: Failed to allocate SVG copy buffer\n");
		return NULL;
	}
	memcpy(svg_copy, data, len);
	svg_copy[len] = '\0';

	// Parse SVG image
	NSVGimage *svg_image = nsvgParse(svg_copy, "px", 96.0f);
	free(svg_copy);

	if (svg_image == NULL) {
		fprintf(stderr, "Error: Failed to parse SVG image\n");
		return NULL;
	}

	// Determine dimensions
	uint32_t width, height;
	if (svg_image->width > 0.0f && svg_image->height > 0.0f) {
		// Use SVG's explicit dimensions
		width = (uint32_t)roundf(svg_image->width);
		height = (uint32_t)roundf(svg_image->height);
	} else {
		// No dimensions specified, use default 512x512
		width = 512;
		height = 512;
		fprintf(stderr, "Warning: SVG has no explicit dimensions, using default 512x512\n");
	}

	// Validate dimensions
	if (width == 0 || height == 0) {
		fprintf(stderr, "Error: Invalid SVG dimensions: %ux%u\n", width, height);
		nsvgDelete(svg_image);
		return NULL;
	}

	if (width > IMAGE_MAX_DIMENSION || height > IMAGE_MAX_DIMENSION) {
		fprintf(stderr, "Error: SVG dimensions exceed maximum (%u): %ux%u\n", IMAGE_MAX_DIMENSION, width, height);
		nsvgDelete(svg_image);
		return NULL;
	}

	// Check pixel count limit
	uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
	if (pixel_count > IMAGE_MAX_PIXELS) {
		fprintf(stderr, "Error: SVG pixel count exceeds maximum (%lu): %llu\n", (unsigned long)IMAGE_MAX_PIXELS, (unsigned long long)pixel_count);
		nsvgDelete(svg_image);
		return NULL;
	}

	// Create rasterizer
	NSVGrasterizer *rast = nsvgCreateRasterizer();
	if (rast == NULL) {
		fprintf(stderr, "Error: Failed to create SVG rasterizer\n");
		nsvgDelete(svg_image);
		return NULL;
	}

	// Create image_t structure
	image_t *output = image_create(width, height);
	if (output == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		nsvgDeleteRasterizer(rast);
		nsvgDelete(svg_image);
		return NULL;
	}

	// Rasterize SVG to pixel buffer
	// Parameters: rasterizer, svg_image, tx, ty, scale, dst, width, height, stride
	nsvgRasterize(rast, svg_image, 0, 0, 1.0f, output->pixels, width, height, width * 4);

	// Cleanup SVG resources
	nsvgDeleteRasterizer(rast);
	nsvgDelete(svg_image);

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
