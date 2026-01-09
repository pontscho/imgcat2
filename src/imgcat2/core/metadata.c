/**
 * @file metadata.c
 * @brief Image metadata output formatting implementation
 */

#include <stdio.h>

#include "metadata.h"

/**
 * @brief Get MIME type string from enum
 *
 * Returns standard MIME type strings for supported image formats.
 */
const char *get_mime_string(mime_type_t mime)
{
	switch (mime) {
		case MIME_PNG: return "image/png";
		case MIME_JPEG: return "image/jpeg";
		case MIME_GIF: return "image/gif";
		case MIME_BMP: return "image/bmp";
		case MIME_WEBP: return "image/webp";
		case MIME_HEIF: return "image/heif";
		case MIME_TIFF: return "image/tiff";
		case MIME_TGA: return "image/x-tga";
		case MIME_PSD: return "image/vnd.adobe.photoshop";
		case MIME_HDR: return "image/vnd.radiance";
		case MIME_PNM: return "image/x-portable-anymap";
		default: return "application/octet-stream";
	}
}

/**
 * @brief Output metadata in human-readable text format
 *
 * Format: "TYPE mime WxH N frame(s)"
 * Example: "PNG image/png 1920x1080 1 frame"
 */
void output_metadata_text(mime_type_t mime, uint32_t width, uint32_t height, int frame_count)
{
	const char *type = mime_type_name(mime);
	const char *mime_str = get_mime_string(mime);

	printf("%s %s %ux%u %d %s\n", type, mime_str, width, height, frame_count, frame_count == 1 ? "frame" : "frames");
}

/**
 * @brief Output metadata as JSON (single line, JSONL format)
 *
 * Format: {"type":"TYPE","mime":"MIME","width":W,"height":H,"frames":N}
 * Example: {"type":"PNG","mime":"image/png","width":1920,"height":1080,"frames":1}
 */
void output_metadata_json(mime_type_t mime, uint32_t width, uint32_t height, int frame_count)
{
	const char *type = mime_type_name(mime);
	const char *mime_str = get_mime_string(mime);

	printf("{\"type\":\"%s\",\"mime\":\"%s\",\"width\":%u,\"height\":%u,\"frames\":%d}\n", type, mime_str, width, height, frame_count);
}
