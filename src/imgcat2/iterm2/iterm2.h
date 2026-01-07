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

/**
 * @brief Check if image format is supported by iTerm2 protocol
 *
 * iTerm2 supports formats that macOS can decode natively:
 * - PNG (Portable Network Graphics)
 * - JPEG (Joint Photographic Experts Group)
 * - GIF (Graphics Interchange Format, including animated)
 * - BMP (Windows Bitmap)
 *
 * @param data Raw image file data
 * @param size Size of data in bytes
 *
 * @return true if format is supported, false otherwise
 *
 * @note Uses magic byte detection from decoders/magic.h
 * @note Returns false if data is NULL or size is 0
 */
bool iterm2_is_format_supported(const uint8_t *data, size_t size);

/**
 * @brief Render image using iTerm2 inline images protocol
 *
 * Sends image to terminal using the iTerm2 inline images protocol
 * (OSC 1337 escape sequence). The image is base64-encoded and sent
 * as a single escape sequence.
 *
 * Protocol format:
 * \033]1337;File=inline=1;size=<bytes>;name=<base64_name>;width=<w>;height=<h>:<base64_data>\a
 *
 * @param data Raw image file data
 * @param size Size of data in bytes
 * @param filename Optional filename for metadata (can be NULL)
 * @param fit_mode If true, fit image to terminal while preserving aspect ratio
 * @param target_width Target width in pixels (-1 for auto/original size)
 * @param target_height Target height in pixels (-1 for auto/original size)
 *
 * @return 0 on success, -1 on error
 *
 * @note For animated GIFs, sends the entire file (iTerm2 handles animation)
 * @note Automatically handles tmux environments with DCS wrapping
 * @note If target_width/height specified: uses pixel dimensions with preserveAspectRatio
 * @note If no dimensions specified: uses original image size
 * @note Outputs to stdout
 */
int iterm2_render(const uint8_t *data, size_t size, const char *filename, bool fit_mode, int target_width, int target_height);

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
