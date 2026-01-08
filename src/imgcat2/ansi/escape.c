/**
 * @file escape.c
 * @brief ANSI escape sequence generation and rendering implementation
 *
 * Implements half-block character rendering with ANSI true color escape
 * sequences. Uses escape sequence caching for common colors to improve
 * performance.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ansi.h"
#include "escape.h"

/**
 * @brief Escape sequence cache (512 bytes)
 *
 * Simple cache for frequently used color escape sequences.
 * Currently unused but reserved for future optimization.
 */
static char escape_cache[512] = { 0 };
static bool cache_initialized = false;

/**
 * @brief Initialize escape sequence cache
 */
void escape_cache_init(void)
{
	if (cache_initialized) {
		return;
	}

	/* Cache initialization (currently a no-op, reserved for future use) */
	memset(escape_cache, 0, sizeof(escape_cache));

	cache_initialized = true;
}

/**
 * @brief Generate ANSI escape sequence for one line (pair of pixel rows)
 */
char *generate_line_ansi(const image_t *img, uint32_t y_top, char *line_buffer)
{
	if (img == NULL || img->pixels == NULL || line_buffer == NULL) {
		return NULL;
	}

	/* Validate y_top is even and within bounds */
	if (y_top % 2 != 0 || y_top >= img->height - 1) {
		return NULL;
	}

	/* Initialize cache on first use */
	if (!cache_initialized) {
		escape_cache_init();
	}

	/* Build ANSI escape sequence line */
	char *ptr = line_buffer;
	size_t remaining = MAX_LINE_BUFFER_SIZE;

	for (uint32_t x = 0; x < img->width; x++) {
		/* Get top pixel (background color) */
		const uint8_t *top_pixel = image_get_pixel(img, x, y_top);
		if (top_pixel == NULL) {
			return NULL;
		}

		/* Get bottom pixel (foreground color + half-block) */
		const uint8_t *bottom_pixel = image_get_pixel(img, x, y_top + 1);
		if (bottom_pixel == NULL) {
			return NULL;
		}

		uint8_t top_r = top_pixel[0];
		uint8_t top_g = top_pixel[1];
		uint8_t top_b = top_pixel[2];
		uint8_t top_a = top_pixel[3];

		uint8_t bottom_r = bottom_pixel[0];
		uint8_t bottom_g = bottom_pixel[1];
		uint8_t bottom_b = bottom_pixel[2];
		uint8_t bottom_a = bottom_pixel[3];

		/* Top pixel → background color */
		int written = 0;
		if (top_a < 128) {
			/* Transparent background */
			written = snprintf(ptr, remaining, ANSI_BG_TRANSPARENT);

		} else {
			/* Opaque background */
			written = snprintf(ptr, remaining, ANSI_BG_RGB, top_r, top_g, top_b);
		}

		if (written < 0 || (size_t)written >= remaining) {
			/* Buffer overflow */
			return NULL;
		}
		ptr += written;
		remaining -= written;

		/* Bottom pixel → foreground color + half-block */
		if (bottom_a < 128) {
			/* Transparent foreground */
			written = snprintf(ptr, remaining, ANSI_FG_TRANSPARENT);

		} else {
			/* Opaque foreground + half-block */
			written = snprintf(ptr, remaining, ANSI_FG_RGB_HALFBLOCK, bottom_r, bottom_g, bottom_b);
		}

		if (written < 0 || (size_t)written >= remaining) {
			/* Buffer overflow */
			return NULL;
		}
		ptr += written;
		remaining -= written;
	}

	/* Append reset + newline */
	int written = snprintf(ptr, remaining, ANSI_RESET "\n");
	if (written < 0 || (size_t)written >= remaining) {
		return NULL;
	}

	return line_buffer;
}

/**
 * @brief Generate ANSI escape sequences for entire frame
 */
char **generate_frame_ansi(const image_t *img, size_t *line_count)
{
	if (img == NULL || img->pixels == NULL || line_count == NULL) {
		return NULL;
	}

	/* Calculate line count (height / 2, round down to even) */
	size_t num_lines = img->height / 2;
	if (num_lines == 0) {
		*line_count = 0;
		return NULL;
	}

	/* Allocate lines array */
	char **lines = malloc(sizeof(char *) * num_lines);
	if (lines == NULL) {
		fprintf(stderr, "generate_frame_ansi: failed to allocate lines array\n");
		return NULL;
	}

	/* Initialize all pointers to NULL for safe cleanup */
	for (size_t i = 0; i < num_lines; i++) {
		lines[i] = NULL;
	}

	/* Generate each line */
	size_t line_index = 0;
	for (uint32_t y = 0; y < img->height - 1; y += 2) {
		/* Allocate line buffer */
		char *line_buffer = malloc(MAX_LINE_BUFFER_SIZE);
		if (line_buffer == NULL) {
			fprintf(stderr, "generate_frame_ansi: failed to allocate line buffer\n");
			free_frame_lines(lines, num_lines);
			return NULL;
		}

		/* Generate ANSI for this line */
		if (generate_line_ansi(img, y, line_buffer) == NULL) {
			fprintf(stderr, "generate_frame_ansi: failed to generate line %zu\n", line_index);
			free(line_buffer);
			free_frame_lines(lines, num_lines);
			return NULL;
		}

		/* Store line in array */
		lines[line_index] = line_buffer;
		line_index++;
	}

	*line_count = num_lines;
	return lines;
}

/**
 * @brief Free frame line buffers
 */
void free_frame_lines(char **lines, size_t line_count)
{
	if (lines == NULL) {
		return;
	}

	/* Free each line buffer */
	for (size_t i = 0; i < line_count; i++) {
		if (lines[i] != NULL) {
			free(lines[i]);
		}
	}

	/* Free lines array */
	free(lines);
}
