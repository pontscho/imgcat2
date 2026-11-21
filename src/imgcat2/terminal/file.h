/**
 * @file file.h
 * @brief File output renderer for image format conversion
 *
 * Provides functionality to render images to files (JPEG, PNG) or stdout,
 * supporting format conversion with quality/compression control.
 */

#ifndef IMGCAT2_FILE_H
#define IMGCAT2_FILE_H

#include <img2/image.h>

#include "../cli.h"

/**
 * @brief Render image to file or stdout
 *
 * Converts and writes image data to a file or stdout in the specified format.
 * Handles:
 * - Animated images (uses first frame or specified frame index)
 * - Format encoding (JPEG, PNG) with quality/compression control
 * - File output with path validation
 * - Stdout output for piping
 *
 * @param frames Array of image frames (RGBA8888 format)
 * @param frame_count Number of frames in array
 * @param opts CLI options containing:
 *   - output_format: Target format (IMG2_FORMAT_JPEG or IMG2_FORMAT_PNG)
 *   - output_file: Output file path, or NULL for stdout
 *   - jpeg_quality: JPEG quality (0-100) if IMG2_FORMAT_JPEG
 *   - png_compression: PNG compression (0-9) if IMG2_FORMAT_PNG
 *   - frame_index: Frame to convert for animated images
 * @return 0 on success, -1 on error
 *
 * @note For animated images, prints warning and uses frame at opts->frame_index
 * @note Validates output_file path for security (path traversal protection)
 * @note Calls img2_encode() to perform actual encoding
 *
 * @example
 * // Convert to JPEG and write to stdout
 * opts.output_format = IMG2_FORMAT_JPEG;
 * opts.jpeg_quality = 90;
 * opts.output_file = NULL;
 * file_render(frames, frame_count, &opts);
 *
 * @example
 * // Convert to PNG and write to file
 * opts.output_format = IMG2_FORMAT_PNG;
 * opts.png_compression = 6;
 * opts.output_file = "output.png";
 * file_render(frames, frame_count, &opts);
 */
int file_render(img2_image_t **frames, int frame_count, const cli_options_t *opts);

#endif /* IMGCAT2_FILE_H */
