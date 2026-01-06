/**
 * @file escape.h
 * @brief ANSI escape sequence generation and rendering API
 *
 * Provides functions for generating ANSI escape sequences for image
 * rendering using half-block characters. Includes caching for common
 * colors and efficient frame-by-frame rendering.
 */

#ifndef IMGCAT2_ESCAPE_H
#define IMGCAT2_ESCAPE_H

#include <stddef.h>
#include <stdint.h>

#include "../core/image.h"

/**
 * @brief Maximum line buffer size (51200 bytes)
 *
 * Large enough for:
 * - 1000 columns × ~50 bytes per cell (ANSI codes + half-block)
 * - Reset sequence + newline
 */
#define MAX_LINE_BUFFER_SIZE 51200

/**
 * @brief Initialize escape sequence cache
 *
 * Initializes the internal cache for frequently used color escape sequences.
 * Cache improves performance by avoiding repeated sprintf calls for common colors.
 *
 * @note Called automatically on first use, but can be called explicitly
 */
void escape_cache_init(void);

/**
 * @brief Generate ANSI escape sequence for one line (pair of pixel rows)
 *
 * Generates ANSI escape codes for a single terminal line using half-block
 * characters (▄). Each terminal line represents two pixel rows:
 * - Top pixel row → background color
 * - Bottom pixel row → foreground color + half-block character
 *
 * @param img Source image (must have even height)
 * @param y_top Top pixel row index (must be even, 0-based)
 * @param line_buffer Pre-allocated buffer (MAX_LINE_BUFFER_SIZE bytes)
 * @return Pointer to line_buffer on success, NULL on error
 *
 * @note Caller must provide line_buffer with MAX_LINE_BUFFER_SIZE bytes
 * @note Pixels with alpha < 128 are treated as transparent
 * @note Line ends with ANSI_RESET + newline
 */
char *generate_line_ansi(const image_t *img, uint32_t y_top, char *line_buffer);

/**
 * @brief Generate ANSI escape sequences for entire frame
 *
 * Generates ANSI escape codes for all lines in the image.
 * Allocates array of line buffers (one per terminal line).
 *
 * @param img Source image
 * @param line_count Output parameter for number of lines generated
 * @return Array of line strings (char**), or NULL on error
 *
 * @note Caller must free returned array with free_frame_lines()
 * @note Each line is a dynamically allocated string (MAX_LINE_BUFFER_SIZE)
 * @note Line count = img->height / 2 (rounds down to even height)
 */
char **generate_frame_ansi(const image_t *img, size_t *line_count);

/**
 * @brief Free frame line buffers
 *
 * Frees the line array and all individual line buffers allocated
 * by generate_frame_ansi().
 *
 * @param lines Array of line strings (from generate_frame_ansi)
 * @param line_count Number of lines in array
 *
 * @note NULL-safe (does nothing if lines is NULL)
 */
void free_frame_lines(char **lines, size_t line_count);

#endif /* IMGCAT2_ESCAPE_H */
