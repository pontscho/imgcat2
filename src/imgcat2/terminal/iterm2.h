/**
 * @file iterm2.h
 * @brief iTerm2 Inline Images Protocol implementation
 *
 * Provides functions for rendering images using iTerm2's native
 * inline images protocol (OSC 1337). Supports both static and
 * animated images with base64 encoding and automatic format detection.
 *
 * The iTerm2 protocol enables high-quality image display by sending
 * the original image file data directly to the terminal, bypassing
 * the need for pixel-level ANSI rendering.
 */

#ifndef IMGCAT2_ITERM2_H
#define IMGCAT2_ITERM2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../core/cli.h"
#include "../core/image.h"

/**
 * @brief Check if image format is supported by iTerm2 protocol
 *
 * All formats supported via decode → scale → encode pipeline.
 * Animated formats with -a flag will force ANSI rendering fallback.
 *
 * Supported formats:
 * - PNG, JPEG, GIF, WebP, HEIF, AVIF, TIFF, JXL, RAW, etc.
 * - All formats decoded and re-encoded as PNG or JPEG
 *
 * Animation handling:
 * - Animated GIF/WebP/APNG/AVIF with opts->animate: force ANSI fallback
 * - Static images: always supported
 *
 * @param data Raw image file data
 * @param size Size of data in bytes
 * @param opts Command-line options (for animation detection)
 *
 * @return true if format is supported, false otherwise
 *
 * @note Uses magic byte detection from decoders/magic.h
 * @note Returns false if data is NULL or size is 0
 * @note Sets opts->force_ansi = true for animated formats with -a flag
 */
bool iterm2_is_format_supported(const uint8_t *data, size_t size, cli_options_t *opts);

/**
 * @brief Render image using iTerm2 inline images protocol
 *
 * Uses decode → scale → encode pipeline to render images.
 * Encodes scaled image to PNG (default) or JPEG format and transmits
 * via OSC 1337 escape sequence. Provides bandwidth optimization by
 * sending scaled image instead of original file.
 *
 * Protocol format:
 * \033]1337;File=inline=1;size=<bytes>;name=<base64_name>:<base64_data>\a
 *
 * @param frames Array of decoded image frames (RGBA format)
 * @param frame_count Number of frames (must be > 0)
 * @param opts Command-line options (iterm2_format, jpeg_quality, filename)
 * @param original_size Original file size in bytes (for bandwidth reporting)
 *
 * @return 0 on success, -1 on error
 *
 * @note Encodes to PNG (lossless, transparency) or JPEG (smaller, no transparency)
 * @note PNG encoding uses compression level 3 for speed
 * @note JPEG encoding uses opts->jpeg_quality (default: 90)
 * @note Dimension limit: 8192x8192 pixels (enforced, fails gracefully)
 * @note Bandwidth savings displayed in non-silent mode
 * @note Automatically handles tmux environments with DCS wrapping
 * @note Outputs to stdout
 */
int iterm2_render(image_t **frames, int frame_count, const cli_options_t *opts, size_t original_size);

/**
 * @brief Check if running inside tmux
 *
 * Detects tmux by checking the TMUX environment variable.
 * When inside tmux, escape sequences must be wrapped with DCS.
 *
 * @return true if TMUX environment variable is set, false otherwise
 */
bool iterm2_is_tmux(void);

#endif /* IMGCAT2_ITERM2_H */
