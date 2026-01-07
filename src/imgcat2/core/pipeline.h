/**
 * @file pipeline.h
 * @brief Image processing pipeline API - secure file I/O and orchestration
 *
 * Provides secure file and stdin reading with path traversal protection,
 * size limit enforcement, safe memory management, and high-level pipeline
 * orchestration functions for reading, decoding, scaling, and rendering.
 */

#ifndef IMGCAT2_PIPELINE_H
#define IMGCAT2_PIPELINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cli.h"
#include "image.h"

/**
 * @brief Read file with path traversal protection and size limits
 *
 * Securely reads a file into memory with:
 * - Path traversal protection (realpath/GetFullPathName)
 * - ".." component validation
 * - File size validation (≤ IMAGE_MAX_FILE_SIZE from image.h)
 * - Safe memory allocation
 *
 * @param path File path to read
 * @param out_data Output parameter for file data (caller must free with free())
 * @param out_size Output parameter for file size in bytes
 *
 * @return true on success, false on error
 *
 * @note Caller must free *out_data with free() when done
 * @note Returns false if path contains "..", file too large, or I/O error
 *
 * @example
 * uint8_t* data;
 * size_t size;
 * if (read_file_secure("image.png", &data, &size)) {
 *     // Process data...
 *     free(data);
 * }
 */
bool read_file_secure(const char *path, uint8_t **out_data, size_t *out_size);

/**
 * @brief Read from stdin with size limits (pipe support)
 *
 * Reads stdin into dynamically allocated buffer with:
 * - Chunk-based reading (4KB chunks)
 * - Dynamic buffer growth (realloc strategy)
 * - Size limit enforcement (≤ IMAGE_MAX_FILE_SIZE)
 * - Graceful EOF handling
 * - TTY detection (expects piped input)
 *
 * @param out_data Output parameter for stdin data (caller must free with free())
 * @param out_size Output parameter for data size in bytes
 *
 * @return true on success, false on error
 *
 * @note Caller must free *out_data with free() when done
 * @note Designed for piped input: cat image.png | imgcat
 * @note Returns false if stdin is TTY, size limit exceeded, or I/O error
 *
 * @example
 * uint8_t* data;
 * size_t size;
 * if (read_stdin_secure(&data, &size)) {
 *     // Process data...
 *     free(data);
 * }
 */
bool read_stdin_secure(uint8_t **out_data, size_t *out_size);

/**
 * @struct target_dimensions_t
 * @brief Terminal-aware target dimensions for image scaling
 */
typedef struct {
	uint32_t width; /**< Target width in pixels */
	uint32_t height; /**< Target height in pixels */
} target_dimensions_t;

/**
 * @brief Calculate target dimensions based on terminal size
 *
 * Calculates image target dimensions for terminal rendering using
 * half-block characters (▀ ▄). Takes into account:
 * - Terminal columns and rows
 * - Vertical doubling factor (half-block renders 2 pixels per row)
 * - Top offset for status line/header
 *
 * Formula:
 * - target_width = cols × RESIZE_FACTOR_X (1:1 horizontal)
 * - target_height = (rows - top_offset) × RESIZE_FACTOR_Y (2:1 vertical)
 *
 * @param cols Terminal columns (character width)
 * @param rows Terminal rows (character height)
 * @param top_offset Reserved rows at top (e.g., for status line)
 *
 * @return Target dimensions struct, or {0, 0} on invalid input
 *
 * @note Returns {0, 0} if calculated dimensions are invalid
 * @note Clamps width to maximum 1000 columns to prevent excessive memory use
 *
 * @example
 * target_dimensions_t dims = calculate_target_dimensions(80, 24, 0);
 * // dims.width = 80, dims.height = 48 (24 * 2)
 */
target_dimensions_t calculate_target_dimensions(uint32_t cols, uint32_t rows, uint32_t top_offset);

/**
 * @brief Read input (file or stdin) based on CLI options
 *
 * Dispatches to read_file_secure() or read_stdin_secure() based on
 * opts->input_file value.
 *
 * @param opts CLI options structure
 * @param out_data Output parameter for file data (caller must free)
 * @param out_size Output parameter for data size
 *
 * @return 0 on success, -1 on error
 */
int pipeline_read(const cli_options_t *opts, uint8_t **out_data, size_t *out_size);

/**
 * @brief Decode image with MIME type detection
 *
 * Detects image format using magic bytes and decodes using appropriate
 * decoder from registry.
 *
 * @param opts CLI options structure (for logging verbosity)
 * @param buffer Input data buffer
 * @param size Input data size
 * @param out_frames Output parameter for decoded frames array (caller must free)
 * @param out_frame_count Output parameter for frame count
 *
 * @return 0 on success, -1 on error
 */
int pipeline_decode(cli_options_t *opts, const uint8_t *buffer, size_t size, image_t ***out_frames, int *out_frame_count);

/**
 * @brief Scale images to terminal dimensions
 *
 * Scales all frames to fit terminal size based on CLI options.
 * Uses terminal_get_size() and calculate_target_dimensions().
 *
 * @param frames Input frames array
 * @param frame_count Number of frames
 * @param opts CLI options (fit_mode, top_offset)
 * @param out_scaled Output parameter for scaled frames array (caller must free)
 *
 * @return 0 on success, -1 on error
 */
int pipeline_scale(image_t **frames, int frame_count, const cli_options_t *opts, image_t ***out_scaled);

/**
 * @brief Render frames to terminal
 *
 * Renders frames to terminal. Dispatches to static or animated rendering
 * based on frame count and CLI options.
 *
 * @param frames Scaled frames array
 * @param frame_count Number of frames
 * @param opts CLI options (animate, fps)
 *
 * @return 0 on success, -1 on error
 */
int pipeline_render(image_t **frames, int frame_count, const cli_options_t *opts);

#endif /* IMGCAT2_PIPELINE_H */
