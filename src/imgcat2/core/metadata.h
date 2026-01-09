/**
 * @file metadata.h
 * @brief Image metadata output formatting
 *
 * Provides functions for outputting image metadata in both human-readable
 * text format and JSON format. Used by the --info CLI option.
 */

#ifndef IMGCAT2_METADATA_H
#define IMGCAT2_METADATA_H

#include <stdint.h>

#include "../decoders/magic.h"

/**
 * @brief Output image metadata in human-readable format
 *
 * Outputs metadata as a single line of text in the format:
 * "TYPE mime WxH N frame(s)"
 *
 * Example: "PNG image/png 1920x1080 1 frame"
 *
 * @param mime MIME type detected
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param frame_count Number of frames (1 for static, N for animated)
 */
void output_metadata_text(mime_type_t mime, uint32_t width, uint32_t height, int frame_count);

/**
 * @brief Output image metadata as JSON (single line)
 *
 * Outputs metadata as a single-line JSON object (JSONL format):
 * {"type":"TYPE","mime":"MIME","width":W,"height":H,"frames":N}
 *
 * Example: {"type":"PNG","mime":"image/png","width":1920,"height":1080,"frames":1}
 *
 * @param mime MIME type detected
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param frame_count Number of frames (1 for static, N for animated)
 */
void output_metadata_json(mime_type_t mime, uint32_t width, uint32_t height, int frame_count);

/**
 * @brief Get MIME type string (e.g., "image/png")
 *
 * Converts mime_type_t enum to standard MIME type string.
 *
 * @param mime MIME type enum
 * @return MIME type string (e.g., "image/png", "image/jpeg")
 */
const char *get_mime_string(mime_type_t mime);

#endif /* IMGCAT2_METADATA_H */
