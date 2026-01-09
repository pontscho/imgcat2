/**
 * @file magic.c
 * @brief Magic bytes detection implementation
 */

#include <stdio.h>
#include <string.h>

#include "magic.h"

/**
 * @brief Magic byte signatures for image formats
 */

/* PNG: 89 50 4E 47 0D 0A 1A 0A */
const uint8_t MAGIC_PNG[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

/* JPEG: FF D8 FF (SOI + start of frame marker) */
const uint8_t MAGIC_JPEG[3] = { 0xFF, 0xD8, 0xFF };

/* GIF signatures */
const char MAGIC_GIF87A[6] = "GIF87a";
const char MAGIC_GIF89A[6] = "GIF89a";

/* BMP: "BM" */
const char MAGIC_BMP[2] = "BM";

/* PSD: "8BPS" */
const char MAGIC_PSD[4] = "8BPS";

/* HDR signatures */
const char MAGIC_HDR_RADIANCE[10] = "#?RADIANCE";
const char MAGIC_HDR_RGBE[6] = "#?RGBE";

/* PNM signatures */
const char MAGIC_PNM_P5[2] = "P5"; /* Grayscale */
const char MAGIC_PNM_P6[2] = "P6"; /* RGB */

/* WebP signatures */
const uint8_t MAGIC_WEBP_RIFF[4] = "RIFF";
const uint8_t MAGIC_WEBP_WEBP[4] = "WEBP";

/**
 * @brief Detect MIME type from binary data magic bytes
 *
 * Performs priority-based signature matching to identify image format.
 * Priority order ensures most common formats are checked first.
 */
mime_type_t detect_mime_type(const uint8_t *data, size_t len)
{
	// Validate input
	if (data == NULL || len < 8) {
		return MIME_UNKNOWN;
	}

	// Priority 1: PNG (exact 8 byte match)
	if (memcmp(data, MAGIC_PNG, 8) == 0) {
		return MIME_PNG;
	}

	// Priority 2: JPEG (3 byte match)
	if (len >= 3 && memcmp(data, MAGIC_JPEG, 3) == 0) {
		return MIME_JPEG;
	}

	// Priority 3: GIF (6 byte match, check GIF87a or GIF89a)
	if (len >= 6) {
		if (memcmp(data, MAGIC_GIF87A, 6) == 0 || memcmp(data, MAGIC_GIF89A, 6) == 0) {
			return MIME_GIF;
		}
	}

	// Priority 3.5: WebP (12 byte match - RIFF + WEBP)
	if (len >= 12) {
		if (memcmp(data, MAGIC_WEBP_RIFF, 4) == 0 && memcmp(data + 8, MAGIC_WEBP_WEBP, 4) == 0) {
			return MIME_WEBP;
		}
	}

	// Priority 4: BMP (2 byte match "BM")
	if (len >= 2 && memcmp(data, MAGIC_BMP, 2) == 0) {
		return MIME_BMP;
	}

	// Priority 5: TGA (header-based detection)
	// TGA format: byte[2] should be 0x02 (uncompressed true-color) or 0x03 (uncompressed grayscale)
	// or 0x0A (RLE true-color) or 0x0B (RLE grayscale)
	if (len >= 18) { // TGA header is 18 bytes
		uint8_t image_type = data[2];
		if (image_type == 0x02 || image_type == 0x03 || image_type == 0x0A || image_type == 0x0B) {
			// Additional validation: byte[16] is pixel depth (should be 8, 16, 24, or 32)
			uint8_t pixel_depth = data[16];
			if (pixel_depth == 8 || pixel_depth == 16 || pixel_depth == 24 || pixel_depth == 32) {
				return MIME_TGA;
			}
		}
	}

	// Priority 6: PSD (4 byte match "8BPS")
	if (len >= 4 && memcmp(data, MAGIC_PSD, 4) == 0) {
		return MIME_PSD;
	}

	// Priority 7: HDR (10 byte or 6 byte match)
	if (len >= 10 && memcmp(data, MAGIC_HDR_RADIANCE, 10) == 0) {
		return MIME_HDR;
	}
	if (len >= 6 && memcmp(data, MAGIC_HDR_RGBE, 6) == 0) {
		return MIME_HDR;
	}

	// Priority 8: PNM (2 byte match "P5" or "P6")
	if (len >= 2) {
		if (memcmp(data, MAGIC_PNM_P5, 2) == 0 || memcmp(data, MAGIC_PNM_P6, 2) == 0) {
			return MIME_PNM;
		}
	}

	// No match found
	return MIME_UNKNOWN;
}

/**
 * @brief Get human-readable name for MIME type
 */
const char *mime_type_name(mime_type_t mime)
{
	switch (mime) {
		case MIME_PNG: return "PNG";
		case MIME_JPEG: return "JPEG";
		case MIME_GIF: return "GIF";
		case MIME_BMP: return "BMP";
		case MIME_TGA: return "TGA";
		case MIME_PSD: return "PSD";
		case MIME_HDR: return "HDR";
		case MIME_PNM: return "PNM";
		case MIME_WEBP: return "WEBP";
		case MIME_UNKNOWN:
		default: return "UNKNOWN";
	}
}
