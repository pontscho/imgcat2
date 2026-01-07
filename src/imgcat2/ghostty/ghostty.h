/**
 * @file ghostty.h
 * @brief Ghostty Terminal Kitty Graphics Protocol implementation
 *
 * Provides functions for rendering images using the Kitty graphics protocol
 * which is supported by Ghostty terminal. Supports both static and animated
 * images with base64 encoding and automatic format detection.
 *
 * The Kitty graphics protocol enables high-quality image display by sending
 * image data directly to the terminal with flexible positioning and scaling.
 */

#ifndef IMGCAT2_GHOSTTY_H
#define IMGCAT2_GHOSTTY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Check if image format is supported by Kitty graphics protocol
 *
 * Kitty protocol supports formats that can be natively decoded:
 * - PNG (Portable Network Graphics)
 * - JPEG (Joint Photographic Experts Group)
 * - GIF (Graphics Interchange Format, including animated)
 *
 * @param data Raw image file data
 * @param size Size of data in bytes
 *
 * @return true if format is supported, false otherwise
 *
 * @note Uses magic byte detection from decoders/magic.h
 * @note Returns false if data is NULL or size is 0
 */
bool ghostty_is_format_supported(const uint8_t *data, size_t size);

/**
 * @brief Render image using Kitty graphics protocol
 *
 * Sends image to terminal using the Kitty graphics protocol.
 * The image is base64-encoded and sent as an escape sequence.
 *
 * Protocol format:
 * \033_G<key>=<value>,<key>=<value>,...;<base64_data>\033\\
 *
 * Key control codes:
 * - a=T: transmit and display
 * - f=100: PNG format (auto-detected from data)
 * - t=d: direct transmission
 * - c=<cols>: width in terminal columns
 * - r=<rows>: height in terminal rows
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
 * @note For animated GIFs, sends the entire file (terminal handles animation)
 * @note Automatically handles tmux environments with DCS wrapping
 * @note If target_width/height specified: converts to terminal cell dimensions
 * @note If no dimensions specified: uses original image size
 * @note Outputs to stdout
 */
int ghostty_render(const uint8_t *data, size_t size, const char *filename, bool fit_mode, int target_width, int target_height);

/**
 * @brief Check if running inside tmux
 *
 * Detects tmux by checking the TMUX environment variable.
 * When inside tmux, escape sequences must be wrapped with DCS.
 *
 * @return true if TMUX environment variable is set, false otherwise
 */
bool ghostty_is_tmux(void);

#endif /* IMGCAT2_GHOSTTY_H */
