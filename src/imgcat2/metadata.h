/**
 * @file metadata.h
 * @brief Image metadata output formatting
 *
 * Provides functions for outputting image metadata in both human-readable
 * text format and JSON format. Used by the --info CLI option.
 */

#ifndef IMGCAT2_METADATA_H
#define IMGCAT2_METADATA_H

#include <stdbool.h>
#include <stdint.h>

#include <img2/mime.h>

/* Forward declarations for EXIF/XMP structures */
#if IMG2_HAS_EXIF
struct img2_exif;
struct img2_xmp;
#endif

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
void output_metadata_text(img2_mime_t mime, uint32_t width, uint32_t height, int frame_count);

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
void output_metadata_json(img2_mime_t mime, uint32_t width, uint32_t height, int frame_count);

#if IMG2_HAS_EXIF
/**
 * @brief Output EXIF metadata in human-readable format
 *
 * Prints EXIF metadata in a structured, readable format with sections
 * for camera info, exposure settings, lens info, and GPS data.
 *
 * @param exif EXIF metadata structure (can be NULL)
 */
void output_exif_text(const struct img2_exif *exif);

/**
 * @brief Output XMP metadata in human-readable format
 *
 * Prints XMP metadata including Dublin Core, IPTC, and Photoshop fields.
 *
 * @param xmp XMP metadata structure (can be NULL)
 */
void output_xmp_text(const struct img2_xmp *xmp);

/**
 * @brief Output EXIF metadata as JSON object
 *
 * Outputs EXIF metadata as a compact JSON object with nested structure
 * for camera, exposure, lens, and GPS data.
 *
 * @param exif EXIF metadata structure (can be NULL)
 * @param first Set to false if this is not the first JSON field (adds leading comma)
 */
void output_exif_json(const struct img2_exif *exif, bool first);

/**
 * @brief Output XMP metadata as JSON object
 *
 * Outputs XMP metadata as a compact JSON object with fields for
 * Dublin Core, IPTC, and Photoshop metadata.
 *
 * @param xmp XMP metadata structure (can be NULL)
 * @param first Set to false if this is not the first JSON field (adds leading comma)
 */
void output_xmp_json(const struct img2_xmp *xmp, bool first);
#endif

#endif /* IMGCAT2_METADATA_H */
