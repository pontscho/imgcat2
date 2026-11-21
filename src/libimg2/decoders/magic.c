/**
 * @file magic.c
 * @brief Magic bytes detection implementation
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "magic.h"

/**
 * @brief Magic byte signatures for image formats
 */

/* PNG: 89 50 4E 47 0D 0A 1A 0A */
static const uint8_t MAGIC_PNG[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };

/* JPEG: FF D8 FF (SOI + start of frame marker) */
static const uint8_t MAGIC_JPEG[3] = { 0xFF, 0xD8, 0xFF };

/* GIF signatures */
static const char MAGIC_GIF87A[6] = "GIF87a";
static const char MAGIC_GIF89A[6] = "GIF89a";

/* BMP: "BM" */
static const char MAGIC_BMP[2] = "BM";

/* PSD: "8BPS" */
static const char MAGIC_PSD[4] = "8BPS";

/* HDR signatures */
static const char MAGIC_HDR_RADIANCE[10] = "#?RADIANCE";
static const char MAGIC_HDR_RGBE[6] = "#?RGBE";

/* WebP signatures */
static const uint8_t MAGIC_WEBP_RIFF[4] = "RIFF";
static const uint8_t MAGIC_WEBP_WEBP[4] = "WEBP";

/* HEIF signatures */
static const uint8_t MAGIC_HEIF_FTYP[4] = "ftyp";
static const uint8_t MAGIC_HEIF_HEIC[4] = "heic";
static const uint8_t MAGIC_HEIF_HEIX[4] = "heix";
static const uint8_t MAGIC_HEIF_HEVC[4] = "hevc";
static const uint8_t MAGIC_HEIF_HEVX[4] = "hevx";
static const uint8_t MAGIC_HEIF_MIF1[4] = "mif1";
static const uint8_t MAGIC_HEIF_J2KI[4] = "j2ki"; /* JPEG 2000 in HEIF: image */
static const uint8_t MAGIC_HEIF_J2IS[4] = "j2is"; /* JPEG 2000 in HEIF: image sequence */

/* AVIF signatures */
static const uint8_t MAGIC_AVIF_AVIF[4] = "avif"; /* AVIF: AV1 Image File Format */
static const uint8_t MAGIC_AVIF_AVIS[4] = "avis"; /* AVIS: AVIF Image Sequence */

/* TIFF signatures */
static const uint8_t MAGIC_TIFF_LE[4] = { 0x49, 0x49, 0x2A, 0x00 }; // "II*\0" little-endian
static const uint8_t MAGIC_TIFF_BE[4] = { 0x4D, 0x4D, 0x00, 0x2A }; // "MM\0*" big-endian

/* RAW format signatures */
static const uint8_t MAGIC_RAW_RAF[16] = "FUJIFILMCCD-RAW"; /* Fuji RAF */
static const uint8_t MAGIC_RAW_ORF_IIRO[4] = "IIRO"; /* Olympus ORF variant 1 */
static const uint8_t MAGIC_RAW_ORF_IIRS[4] = "IIRS"; /* Olympus ORF variant 2 */
static const uint8_t MAGIC_RAW_RW2[4] = { 'I', 'I', 'U', 0x00 }; /* Panasonic RW2 */
static const uint8_t MAGIC_RAW_CR2[4] = { 'C', 'R', 0x02, 0x00 }; /* Canon CR2 marker at offset 8 */

/* QOI signature */
static const uint8_t MAGIC_QOI[4] = "qoif"; /* QOI: Quite OK Image format */

/* JXL signatures */
static const uint8_t MAGIC_JXL_BARE[2] = { 0xff, 0x0a }; /* Bare codestream */
static const uint8_t MAGIC_JXL_CONTAINER[12] = { 0x00, 0x00, 0x00, 0x0c, 0x4a, 0x58, 0x4c, 0x20, 0x0d, 0x0a, 0x87, 0x0a }; /* ISOBMFF container */

/* SVG signatures */
static const uint8_t MAGIC_SVG_XML[5] = "<?xml"; /* XML declaration */
static const uint8_t MAGIC_SVG_ROOT[4] = "<svg"; /* Direct SVG root */
static const uint8_t MAGIC_SVG_BOM_XML[8] = { 0xEF, 0xBB, 0xBF, '<', '?', 'x', 'm', 'l' }; /* BOM + XML */

/**
 * @brief Check if TIFF data is actually a TIFF-based RAW format
 *
 * Some RAW formats (CR2, NEF, ARW, DNG) use TIFF container structure.
 * This function detects explicit RAW markers to distinguish them from regular TIFF.
 *
 * @param data Pointer to file data buffer
 * @param len Length of data buffer in bytes
 * @return true if detected as TIFF-based RAW, false otherwise
 *
 * @note CR2 (Canon) has explicit "CR\x02\x00" marker at offset 8
 * @note NEF/ARW/DNG: detected by scanning TIFF IFD for camera manufacturer strings
 */
static bool is_tiff_based_raw(const uint8_t *data, size_t len)
{
	if (len < 16) {
		return false;
	}

	// CR2 (Canon) - explicit marker at offset 8
	if (memcmp(data + 8, MAGIC_RAW_CR2, 4) == 0) {
		return true;
	}

	// Check for camera manufacturer strings in TIFF header
	// NEF (Nikon), ARW (Sony), DNG, and other RAW formats contain
	// manufacturer information in the TIFF IFD structure
	// We scan the first 2048 bytes for these strings (heuristic approach)
	// Note: DNG "Adobe" string can be at offset ~800-1000 bytes
	size_t scan_len = (len > 2048) ? 2048 : len;

	// Common camera manufacturer strings found in RAW files
	const char *raw_markers[] = {
		"NIKON", // Nikon NEF
		"SONY", // Sony ARW
		"Canon", // Canon (additional to CR2 marker)
		"OLYMPUS", // Olympus ORF (TIFF-based)
		"Panasonic", // Panasonic RW2 (TIFF-based)
		"PENTAX", // Pentax PEF
		"LEICA", // Leica RAW
		"Phase One", // Phase One IIQ
		"Hasselblad", // Hasselblad 3FR
		"Kodak", // Kodak DCS/DCR
		"Mamiya", // Mamiya MEF
		"Samsung", // Samsung SRW
		"SIGMA", // Sigma X3F
		"Adobe DNG", // Adobe DNG (contains "Adobe DNG Converter" string)
		"DNG", // Generic DNG marker
	};

	for (size_t i = 0; i < sizeof(raw_markers) / sizeof(raw_markers[0]); i++) {
		if (memmem(data, scan_len, raw_markers[i], strlen(raw_markers[i])) != NULL) {
			return true;
		}
	}

	// For ambiguous cases, default to regular TIFF
	return false;
}

/**
 * @brief Check if an ISOBMFF ftyp box declares a JPEG 2000 HEIF brand
 *
 * JPEG 2000 in HEIF (ISO/IEC 15444-16) uses the 'j2ki' (image) and 'j2is'
 * (image sequence) brands, either as major brand or among the compatible
 * brands (e.g. major 'mif1' or a vendor brand).
 *
 * @param data Pointer to file data buffer (starts with the ftyp box)
 * @param len Length of data buffer in bytes
 * @return true if a JPEG 2000 HEIF brand is present, false otherwise
 *
 * @note Compatible brands start at offset 16 and are scanned only within
 *       both the ftyp box size and the available data
 */
static bool is_heif_j2k_ftyp(const uint8_t *data, size_t len)
{
	if (len < 12 || memcmp(data + 4, MAGIC_HEIF_FTYP, 4) != 0) {
		return false;
	}

	// Major brand at offset 8
	if (memcmp(data + 8, MAGIC_HEIF_J2KI, 4) == 0 || memcmp(data + 8, MAGIC_HEIF_J2IS, 4) == 0) {
		return true;
	}

	// Compatible brands follow major brand (4) and minor version (4)
	size_t box_size = ((size_t)data[0] << 24) | ((size_t)data[1] << 16) | ((size_t)data[2] << 8) | (size_t)data[3];
	size_t end = (box_size < len) ? box_size : len;
	for (size_t off = 16; off + 4 <= end; off += 4) {
		if (memcmp(data + off, MAGIC_HEIF_J2KI, 4) == 0 || memcmp(data + off, MAGIC_HEIF_J2IS, 4) == 0) {
			return true;
		}
	}

	return false;
}

/**
 * @brief Detect MIME type from binary data magic bytes
 *
 * Performs priority-based signature matching to identify image format.
 * Priority order ensures most common formats are checked first.
 */
img2_mime_t img2_detect_mime(const uint8_t *data, size_t len)
{
	// Validate input
	if (data == NULL || len < 8) {
		return IMG2_MIME_UNKNOWN;
	}

	// Priority 1: PNG (exact 8 byte match)
	if (memcmp(data, MAGIC_PNG, 8) == 0) {
		return IMG2_MIME_PNG;
	}

	// Priority 2: JPEG (3 byte match)
	if (len >= 3 && memcmp(data, MAGIC_JPEG, 3) == 0) {
		return IMG2_MIME_JPEG;
	}

	// Priority 3: GIF (6 byte match, check GIF87a or GIF89a)
	if (len >= 6) {
		if (memcmp(data, MAGIC_GIF87A, 6) == 0 || memcmp(data, MAGIC_GIF89A, 6) == 0) {
			return IMG2_MIME_GIF;
		}
	}

	// Priority 3.5: WebP (12 byte match - RIFF + WEBP)
	if (len >= 12) {
		if (memcmp(data, MAGIC_WEBP_RIFF, 4) == 0 && memcmp(data + 8, MAGIC_WEBP_WEBP, 4) == 0) {
			return IMG2_MIME_WEBP;
		}
	}

	// Priority 3.6: JXL (JPEG XL) - check container first, then bare
	if (len >= 12) {
		if (memcmp(data, MAGIC_JXL_CONTAINER, 12) == 0) {
			return IMG2_MIME_JXL;
		}
	}
	if (len >= 2) {
		if (memcmp(data, MAGIC_JXL_BARE, 2) == 0) {
			return IMG2_MIME_JXL;
		}
	}

	// Priority 3.65: SVG (XML-based vector format)
	// Check for direct <svg root tag first (most common in standalone SVG files)
	if (len >= 4) {
		if (memcmp(data, MAGIC_SVG_ROOT, 4) == 0) {
			return IMG2_MIME_SVG;
		}
	}
	// BOM + XML check (8 bytes, for SVG files with XML declaration)
	if (len >= 8) {
		if (memcmp(data, MAGIC_SVG_BOM_XML, 8) == 0 || memcmp(data, MAGIC_SVG_XML, 5) == 0) {
			if (memmem(data, len, MAGIC_SVG_ROOT, 4) != NULL) {
				return IMG2_MIME_SVG;
			}
		}
	}

	// Priority 3.7: HEIF/HEIC (12+ byte match - ftyp box + brand)
	if (len >= 12) {
		if (memcmp(data + 4, MAGIC_HEIF_FTYP, 4) == 0) {
			// Check major brand at offset 8
			const uint8_t *brand = data + 8;
			if (memcmp(brand, MAGIC_HEIF_HEIC, 4) == 0 || memcmp(brand, MAGIC_HEIF_HEIX, 4) == 0 || memcmp(brand, MAGIC_HEIF_HEVC, 4) == 0 || memcmp(brand, MAGIC_HEIF_HEVX, 4) == 0 || memcmp(brand, MAGIC_HEIF_MIF1, 4) == 0) {
				return IMG2_MIME_HEIF;
			}
		}
		// JPEG 2000 in HEIF ('j2ki' / 'j2is' major or compatible brand)
		if (is_heif_j2k_ftyp(data, len)) {
			return IMG2_MIME_HEIF;
		}
	}

	// Priority 3.75: AVIF (12+ byte match - ftyp box + brand)
	if (len >= 12) {
		if (memcmp(data + 4, MAGIC_HEIF_FTYP, 4) == 0) {
			// Check major brand at offset 8
			const uint8_t *brand = data + 8;
			if (memcmp(brand, MAGIC_AVIF_AVIF, 4) == 0 || memcmp(brand, MAGIC_AVIF_AVIS, 4) == 0) {
				return IMG2_MIME_AVIF;
			}
		}
	}

	// Priority 3.8: TIFF and TIFF-based RAW (4 byte match - II or MM marker)
	if (len >= 4) {
		if (memcmp(data, MAGIC_TIFF_LE, 4) == 0 || memcmp(data, MAGIC_TIFF_BE, 4) == 0) {
			// Check if this is a TIFF-based RAW format (CR2, NEF, ARW, DNG)
			if (is_tiff_based_raw(data, len)) {
				return IMG2_MIME_RAW;
			}
			// Regular TIFF file
			return IMG2_MIME_TIFF;
		}
	}

	// Priority 3.9: Non-TIFF RAW formats
	if (len >= 16) {
		// Fuji RAF - 16 byte signature
		if (memcmp(data, MAGIC_RAW_RAF, 16) == 0) {
			return IMG2_MIME_RAW;
		}
	}

	if (len >= 4) {
		// Olympus ORF - 4 byte signature (two variants)
		if (memcmp(data, MAGIC_RAW_ORF_IIRO, 4) == 0 || memcmp(data, MAGIC_RAW_ORF_IIRS, 4) == 0) {
			return IMG2_MIME_RAW;
		}

		// Panasonic RW2 - 4 byte signature
		if (memcmp(data, MAGIC_RAW_RW2, 4) == 0) {
			return IMG2_MIME_RAW;
		}

		// QOI - 4 byte signature "qoif"
		if (memcmp(data, MAGIC_QOI, 4) == 0) {
			return IMG2_MIME_QOI;
		}
	}

	// Priority 4: BMP (2 byte match "BM")
	if (len >= 2 && memcmp(data, MAGIC_BMP, 2) == 0) {
		return IMG2_MIME_BMP;
	}

	// Priority 4.5: ICO/CUR (with type field discrimination)
	if (len >= 6 && data[0] == 0x00 && data[1] == 0x00) {
		// Type field (bytes 2-3, little-endian)
		uint16_t type = (uint16_t)(data[2] | (data[3] << 8));

		// Image count validation (bytes 4-5)
		uint16_t count = (uint16_t)(data[4] | (data[5] << 8));

		if (count > 0 && count < 256) {
			if (type == 1) {
				return IMG2_MIME_ICO; // Icon
			} else if (type == 2) {
				return IMG2_MIME_CUR; // Cursor
			}
		}
	}

	// Priority 4.8: PNM/PAM ("P1".."P7" followed by whitespace)
	// Must run before the TGA heuristic: a Netpbm header such as
	// "P6\n# comment..." has '\n' (0x0A) at offset 2 and may have a space
	// (0x20) at offset 16, which the TGA check would accept.
	if (len >= 3 && data[0] == 'P' && data[1] >= '1' && data[1] <= '7') {
		uint8_t c = data[2];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') {
			return IMG2_MIME_PNM;
		}
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
				return IMG2_MIME_TGA;
			}
		}
	}

	// Priority 6: PSD (4 byte match "8BPS")
	if (len >= 4 && memcmp(data, MAGIC_PSD, 4) == 0) {
		return IMG2_MIME_PSD;
	}

	// Priority 7: HDR (10 byte or 6 byte match)
	if (len >= 10 && memcmp(data, MAGIC_HDR_RADIANCE, 10) == 0) {
		return IMG2_MIME_HDR;
	}
	if (len >= 6 && memcmp(data, MAGIC_HDR_RGBE, 6) == 0) {
		return IMG2_MIME_HDR;
	}

	// No match found
	return IMG2_MIME_UNKNOWN;
}

/**
 * @brief Get human-readable name for MIME type
 */
const char *img2_mime_name(img2_mime_t mime)
{
	switch (mime) {
		case IMG2_MIME_PNG: return "PNG";
		case IMG2_MIME_JPEG: return "JPEG";
		case IMG2_MIME_GIF: return "GIF";
		case IMG2_MIME_BMP: return "BMP";
		case IMG2_MIME_TGA: return "TGA";
		case IMG2_MIME_PSD: return "PSD";
		case IMG2_MIME_HDR: return "HDR";
		case IMG2_MIME_PNM: return "PNM";
		case IMG2_MIME_WEBP: return "WEBP";
		case IMG2_MIME_HEIF: return "HEIF";
		case IMG2_MIME_AVIF: return "AVIF";
		case IMG2_MIME_TIFF: return "TIFF";
		case IMG2_MIME_RAW: return "RAW";
		case IMG2_MIME_QOI: return "QOI";
		case IMG2_MIME_ICO: return "ICO";
		case IMG2_MIME_CUR: return "CUR";
		case IMG2_MIME_JXL: return "JXL";
		case IMG2_MIME_SVG: return "SVG";
		case IMG2_MIME_YUV: return "YUV";
		case IMG2_MIME_UNKNOWN:
		default: return "UNKNOWN";
	}
}

/**
 * @brief Get MIME type string from enum
 *
 * Returns standard MIME type strings for supported image formats.
 */
const char *img2_mime_string(img2_mime_t mime)
{
	switch (mime) {
		case IMG2_MIME_PNG: return "image/png";
		case IMG2_MIME_JPEG: return "image/jpeg";
		case IMG2_MIME_GIF: return "image/gif";
		case IMG2_MIME_BMP: return "image/bmp";
		case IMG2_MIME_WEBP: return "image/webp";
		case IMG2_MIME_HEIF: return "image/heif";
		case IMG2_MIME_AVIF: return "image/avif";
		case IMG2_MIME_JXL: return "image/jxl";
		case IMG2_MIME_QOI: return "image/qoi";
		case IMG2_MIME_ICO: return "image/vnd.microsoft.icon";
		case IMG2_MIME_CUR: return "image/x-win-bitmap";
		case IMG2_MIME_SVG: return "image/svg+xml";
		case IMG2_MIME_TIFF: return "image/tiff";
		case IMG2_MIME_TGA: return "image/x-tga";
		case IMG2_MIME_PSD: return "image/vnd.adobe.photoshop";
		case IMG2_MIME_HDR: return "image/vnd.radiance";
		case IMG2_MIME_PNM: return "image/x-portable-anymap";
		case IMG2_MIME_YUV: return "application/octet-stream"; /* headerless raw frame, no registered image type */
		default: return "application/octet-stream";
	}
}
