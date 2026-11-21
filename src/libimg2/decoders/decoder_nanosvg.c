/**
 * @file decoder_nanosvg.c
 * @brief SVG decoder using nanosvg library
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

#include "../text/font_manager.h"
#include "decoder.h"

/* NanoSVG implementation */
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

/**
 * @brief Simple CSS class to inline style converter for SVG
 *
 * Converts CSS classes to inline styles since nanosvg doesn't support CSS.
 * This is a simple implementation that handles basic class selectors.
 *
 * @param svg_data Input SVG string
 * @return Modified SVG string with inline styles, or NULL on error
 * @note Caller must free the returned string
 */
static char *inline_svg_styles(const char *svg_data)
{
	// Find <style> tag
	const char *style_start = strstr(svg_data, "<style>");
	const char *style_end = strstr(svg_data, "</style>");

	if (style_start == NULL || style_end == NULL) {
		// No CSS to inline, return copy of original
		return strdup(svg_data);
	}

	style_start += 7; // skip "<style>"
	size_t style_len = style_end - style_start;

	// Extract CSS content
	char *css = (char *)malloc(style_len + 1);
	if (css == NULL) {
		return NULL;
	}
	memcpy(css, style_start, style_len);
	css[style_len] = '\0';

	// Create output buffer (estimate 3x size for inline styles)
	size_t svg_len = strlen(svg_data);
	size_t out_size = svg_len * 3;
	char *output = (char *)malloc(out_size);
	if (output == NULL) {
		free(css);
		return NULL;
	}

	size_t out_pos = 0;
	const char *read_pos = svg_data;

	// Copy everything before <style> tag and skip style section
	const char *style_tag_start = strstr(svg_data, "<style>");
	if (style_tag_start != NULL) {
		size_t pre_style_len = style_tag_start - svg_data;
		memcpy(output, svg_data, pre_style_len);
		out_pos = pre_style_len;
		read_pos = style_end + 8; // skip "</style>"
	}

	// Parse and apply CSS classes
	while (*read_pos != '\0') {
		// Find next class attribute
		const char *class_start = strstr(read_pos, "class=\"");
		if (class_start == NULL) {
			// No more classes, copy rest
			size_t remaining = strlen(read_pos);
			if (out_pos + remaining + 1 > out_size) {
				out_size = (out_pos + remaining + 1) * 2;
				char *new_output = (char *)realloc(output, out_size);
				if (new_output == NULL) {
					free(output);
					free(css);
					return NULL;
				}
				output = new_output;
			}
			memcpy(output + out_pos, read_pos, remaining + 1); // +1 for null terminator
			out_pos += remaining;
			break;
		}

		// Copy up to class attribute
		size_t copy_len = class_start - read_pos;
		if (out_pos + copy_len > out_size) {
			out_size = (out_pos + copy_len + 4096) * 2;
			char *new_output = (char *)realloc(output, out_size);
			if (new_output == NULL) {
				free(output);
				free(css);
				return NULL;
			}
			output = new_output;
		}
		memcpy(output + out_pos, read_pos, copy_len);
		out_pos += copy_len;

		// Extract class names
		const char *class_value = class_start + 7; // skip 'class="'
		const char *class_end = strchr(class_value, '"');
		if (class_end == NULL) {
			read_pos = class_start + 7;
			continue;
		}

		size_t class_len = class_end - class_value;
		char classes[512];
		if (class_len >= sizeof(classes)) {
			class_len = sizeof(classes) - 1;
		}
		memcpy(classes, class_value, class_len);
		classes[class_len] = '\0';

		// Build inline style from CSS
		char style_buffer[4096] = "";
		size_t style_pos = 0;

		// Parse space-separated class names manually (thread-safe)
		const char *c = classes;
		while (*c != '\0' && style_pos < sizeof(style_buffer) - 1) {
			// Skip whitespace
			while (*c == ' ' || *c == '\t' || *c == '\n') {
				c++;
			}
			if (*c == '\0') {
				break;
			}

			// Find end of class name
			const char *class_name_start = c;
			while (*c != '\0' && *c != ' ' && *c != '\t' && *c != '\n') {
				c++;
			}
			size_t class_name_len = c - class_name_start;

			// Look up CSS rule for this class
			char class_selector[256];
			if (class_name_len < sizeof(class_selector) - 10) {
				class_selector[0] = '.';
				memcpy(class_selector + 1, class_name_start, class_name_len);
				class_selector[class_name_len + 1] = ' ';
				class_selector[class_name_len + 2] = '{';
				class_selector[class_name_len + 3] = '\0';

				const char *rule_start = strstr(css, class_selector);
				if (rule_start != NULL) {
					rule_start += class_name_len + 3; // skip ".classname {"
					const char *rule_end = strchr(rule_start, '}');
					if (rule_end != NULL) {
						size_t rule_len = rule_end - rule_start;
						if (style_pos + rule_len < sizeof(style_buffer) - 1) {
							memcpy(style_buffer + style_pos, rule_start, rule_len);
							style_pos += rule_len;
						}
					}
				}
			}
		}
		style_buffer[style_pos] = '\0';

		// Add style attribute if we found CSS rules
		if (style_pos > 0) {
			// Trim whitespace from style
			const char *style_start = style_buffer;
			while (*style_start == ' ' || *style_start == '\n' || *style_start == '\t') {
				style_start++;
			}
			size_t style_len = strlen(style_start);
			while (style_len > 0 && (style_start[style_len - 1] == ' ' || style_start[style_len - 1] == '\n' || style_start[style_len - 1] == '\t')) {
				style_len--;
			}

			// Ensure buffer is large enough
			size_t needed = 10 + style_len; // " style=\"" + content + "\""
			if (out_pos + needed > out_size) {
				out_size = (out_pos + needed + 4096) * 2;
				char *new_output = (char *)realloc(output, out_size);
				if (new_output == NULL) {
					free(output);
					free(css);
					return NULL;
				}
				output = new_output;
			}

			// Write style attribute
			memcpy(output + out_pos, " style=\"", 8);
			out_pos += 8;
			memcpy(output + out_pos, style_start, style_len);
			out_pos += style_len;
			output[out_pos++] = '"';
		}

		// Skip the class attribute
		read_pos = class_end + 1;
	}

	output[out_pos] = '\0';
	free(css);
	return output;
}

/**
 * @brief Decode SVG image using nanosvg (single frame)
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
image_t **decode_svg_nanosvg(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_svg_nanosvg\n");
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

	// Convert CSS classes to inline styles (nanosvg doesn't support CSS)
	char *svg_processed = inline_svg_styles(svg_copy);
	free(svg_copy);
	if (svg_processed == NULL) {
		fprintf(stderr, "Error: Failed to process SVG styles\n");
		return NULL;
	}

	// Initialize font manager (for text rendering)
	static bool font_mgr_initialized = false;
	if (!font_mgr_initialized) {
		if (font_manager_init()) {
			font_mgr_initialized = true;
		}
	}

	// Parse SVG image
	NSVGimage *svg_image = nsvgParse(svg_processed, "px", 96.0f);
	free(svg_processed);

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
