/**
 * @file magic.h
 * @brief Magic bytes detection and MIME type identification
 *
 * Provides magic byte signatures and detection API for identifying
 * image file formats from binary data headers.
 */

#ifndef IMGCAT2_MAGIC_H
#define IMGCAT2_MAGIC_H

#include <stddef.h>
#include <stdint.h>

/**
 * @enum mime_type_t
 * @brief Supported image MIME types
 */

typedef enum {
	MIME_UNKNOWN = 0, /**< Unknown or unsupported format */
	MIME_PNG, /**< PNG: Portable Network Graphics */
	MIME_JPEG, /**< JPEG: Joint Photographic Experts Group */
	MIME_GIF, /**< GIF: Graphics Interchange Format */
	MIME_BMP, /**< BMP: Windows Bitmap */
	MIME_TGA, /**< TGA: Truevision Targa */
	MIME_PSD, /**< PSD: Adobe Photoshop Document */
	MIME_HDR, /**< HDR: Radiance RGBE */
	MIME_PNM, /**< PNM: Portable Anymap (PBM/PGM/PPM) */
	MIME_WEBP, /**< WEBP: WebP image format */
	MIME_HEIF, /**< HEIF: High Efficiency Image Format */
	MIME_TIFF, /**< TIFF: Tagged Image File Format */
	MIME_RAW, /**< RAW: Camera RAW formats (CR2, NEF, ARW, DNG, etc.) */
	MIME_QOI, /**< QOI: Quite OK Image format */
	MIME_ICO, /**< ICO: Windows Icon format */
	MIME_CUR, /**< CUR: Windows Cursor format */
} mime_type_t;

/**
 * @brief Magic byte signatures for image formats
 *
 * These signatures are checked at the beginning of file data
 * to identify the image format.
 */

/** PNG signature: 8 bytes */
extern const uint8_t MAGIC_PNG[8];

/** JPEG signature: 3 bytes (SOI marker + start of frame) */
extern const uint8_t MAGIC_JPEG[3];

/** GIF87a signature: 6 bytes */
extern const char MAGIC_GIF87A[6];

/** GIF89a signature: 6 bytes */
extern const char MAGIC_GIF89A[6];

/** BMP signature: 2 bytes */
extern const char MAGIC_BMP[2];

/** PSD signature: 4 bytes */
extern const char MAGIC_PSD[4];

/** HDR Radiance signature: 10 bytes */
extern const char MAGIC_HDR_RADIANCE[10];

/** HDR RGBE signature: 6 bytes */
extern const char MAGIC_HDR_RGBE[6];

/** PNM P5 signature (grayscale): 2 bytes */
extern const char MAGIC_PNM_P5[2];

/** PNM P6 signature (RGB): 2 bytes */
extern const char MAGIC_PNM_P6[2];

/** WebP RIFF signature: 4 bytes */
extern const uint8_t MAGIC_WEBP_RIFF[4];

/** WebP WEBP signature: 4 bytes */
extern const uint8_t MAGIC_WEBP_WEBP[4];

/** HEIF ftyp signature: 4 bytes */
extern const uint8_t MAGIC_HEIF_FTYP[4];

/** HEIF HEIC brand: 4 bytes */
extern const uint8_t MAGIC_HEIF_HEIC[4];

/** HEIF HEIX brand: 4 bytes */
extern const uint8_t MAGIC_HEIF_HEIX[4];

/** HEIF HEVC brand: 4 bytes */
extern const uint8_t MAGIC_HEIF_HEVC[4];

/** HEIF HEVX brand: 4 bytes */
extern const uint8_t MAGIC_HEIF_HEVX[4];

/** HEIF MIF1 brand: 4 bytes */
extern const uint8_t MAGIC_HEIF_MIF1[4];

/** TIFF little-endian signature: 4 bytes */
extern const uint8_t MAGIC_TIFF_LE[4];

/** TIFF big-endian signature: 4 bytes */
extern const uint8_t MAGIC_TIFF_BE[4];

/** RAF (Fuji) signature: 16 bytes */
extern const uint8_t MAGIC_RAW_RAF[16];

/** ORF (Olympus) IIRO signature: 4 bytes */
extern const uint8_t MAGIC_RAW_ORF_IIRO[4];

/** ORF (Olympus) IIRS signature: 4 bytes */
extern const uint8_t MAGIC_RAW_ORF_IIRS[4];

/** RW2 (Panasonic) signature: 4 bytes */
extern const uint8_t MAGIC_RAW_RW2[4];

/** CR2 (Canon) marker at offset 8: 4 bytes */
extern const uint8_t MAGIC_RAW_CR2[4];

/** QOI (Quite OK Image) signature: 4 bytes */
extern const uint8_t MAGIC_QOI[4];

/** ICO (Windows Icon) signature: 4 bytes */
extern const uint8_t MAGIC_ICO[4];

/** CUR (Windows Cursor) signature: 4 bytes */
extern const uint8_t MAGIC_CUR[4];

/**
 * @brief Detect MIME type from binary data magic bytes
 *
 * Examines the first bytes of image data to identify the format
 * based on magic byte signatures.
 *
 * Detection priority order:
 * 1. PNG (8 byte signature)
 * 2. JPEG (3 byte signature)
 * 3. GIF (6 byte signature, GIF87a or GIF89a)
 * 4. BMP (2 byte signature "BM")
 * 5. TGA (header-based: byte[2] and byte[16])
 * 6. PSD (4 byte signature "8BPS")
 * 7. HDR (10 or 6 byte signature)
 * 8. PNM (2 byte signature "P5" or "P6")
 *
 * @param data Pointer to file data buffer
 * @param len Length of data buffer in bytes
 * @return Detected mime_type_t, or MIME_UNKNOWN if not recognized
 *
 * @note Requires at least 8 bytes of data for reliable detection
 * @note Returns MIME_UNKNOWN if len < 8 or no signature matches
 *
 * @example
 * uint8_t* data;
 * size_t size;
 * if (read_file_secure("image.png", &data, &size)) {
 *     mime_type_t mime = detect_mime_type(data, size);
 *     if (mime == MIME_PNG) {
 *         printf("PNG image detected\n");
 *     }
 *     free(data);
 * }
 */
mime_type_t detect_mime_type(const uint8_t *data, size_t len);

/**
 * @brief Get human-readable name for MIME type
 *
 * @param mime MIME type enum value
 * @return String name (e.g., "PNG", "JPEG"), or "UNKNOWN"
 */
const char *mime_type_name(mime_type_t mime);

#endif /* IMGCAT2_MAGIC_H */
