/**
 * @file kitty.h
 * @brief Terminal Kitty Graphics Protocol implementation
 *
 * Provides functions for rendering images using the Kitty graphics protocol
 * which is supported by Ghostty terminal. Supports both static and animated
 * images with base64 encoding and automatic format detection.
 *
 * The Kitty graphics protocol enables high-quality image display by sending
 * image data directly to the terminal with flexible positioning and scaling.
 */

#ifndef IMGCAT2_KITTY_H
#define IMGCAT2_KITTY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Check if image format is supported by Kitty graphics protocol
 *
 * Kitty graphics protocol supports multiple transmission formats:
 * - f=100: PNG format (sent directly, no decoding)
 * - f=32: RGBA raw pixel data (decoded from JPEG/GIF)
 * - f=24: RGB raw pixel data (not currently used)
 *
 * Supported formats:
 * - PNG (Portable Network Graphics) - sent directly with f=100
 * - JPEG (Joint Photographic Experts Group) - decoded to RGBA and sent with f=32
 * - Static GIF (single frame) - decoded to RGBA and sent with f=32
 *
 * Not supported (falls back to ANSI):
 * - Animated GIF (multiple frames) - requires frame-by-frame ANSI rendering
 *
 * @param data Raw image file data
 * @param size Size of data in bytes
 * @param opts Command-line options (used to check for animation support)
 *
 * @return true if format is supported, false otherwise
 *
 * @note Uses magic byte detection from decoders/magic.h
 * @note Uses gif_is_animated() to detect animated GIFs
 * @note Returns false if data is NULL or size is 0
 */
bool kitty_is_format_supported(const uint8_t *data, size_t size, cli_options_t *opts);

/**
 * @brief Render image using Kitty graphics protocol
 *
 * Sends image to terminal using the Kitty graphics protocol with automatic
 * format detection and optimal transmission method selection.
 *
 * Protocol format:
 * \033_G<key>=<value>,<key>=<value>,...;<base64_data>\033\\
 *
 * Transmission methods:
 * - PNG: Sends raw PNG data with f=100 (no decoding)
 * - JPEG: Decodes to RGBA, sends with f=32,s=width,v=height
 * - Static GIF: Decodes to RGBA, sends with f=32,s=width,v=height
 *
 * Key control codes:
 * - a=T: transmit and display
 * - f=100: PNG format (direct transmission)
 * - f=32: RGBA raw pixel data (decoded transmission)
 * - t=d: direct transmission
 * - s=<width>,v=<height>: pixel dimensions (required for f=32)
 * - c=<cols>: width in terminal columns (optional sizing)
 * - r=<rows>: height in terminal rows (optional sizing)
 *
 * @param frames Array of image frames
 * @param frame_count Number of frames in the array
 * @param opts Command-line options for rendering
 *
 * @return 0 on success, -1 on error
 *
 * @note Automatically handles tmux environments with DCS wrapping
 * @note PNG images bypass decoding for maximum efficiency
 * @note JPEG/GIF images are decoded once and transmitted as RGBA
 * @note Animated GIFs should not reach this function (filtered by kitty_is_format_supported)
 * @note If target_width/height specified: converts to terminal cell dimensions
 * @note Outputs to stdout
 */
int kitty_render(image_t **frames, int frame_count, const cli_options_t *opts);

#endif /* IMGCAT2_KITTY_H */
