/**
 * @file magic.h
 * @brief Magic bytes detection and MIME type identification
 *
 * Provides magic byte signatures and detection API for identifying
 * image file formats from binary data headers.
 */

#ifndef IMG2_INTERNAL_DECODERS_MAGIC_H
#define IMG2_INTERNAL_DECODERS_MAGIC_H

#include <stddef.h>
#include <stdint.h>

/**
 * @enum img2_mime_t
 * @brief Supported image MIME types
 */

typedef enum {
	IMG2_MIME_UNKNOWN = 0, /**< Unknown or unsupported format */
	IMG2_MIME_PNG, /**< PNG: Portable Network Graphics */
	IMG2_MIME_JPEG, /**< JPEG: Joint Photographic Experts Group */
	IMG2_MIME_GIF, /**< GIF: Graphics Interchange Format */
	IMG2_MIME_BMP, /**< BMP: Windows Bitmap */
	IMG2_MIME_TGA, /**< TGA: Truevision Targa */
	IMG2_MIME_PSD, /**< PSD: Adobe Photoshop Document */
	IMG2_MIME_HDR, /**< HDR: Radiance RGBE */
	IMG2_MIME_PNM, /**< PNM: Portable Anymap (PBM/PGM/PPM) */
	IMG2_MIME_WEBP, /**< WEBP: WebP image format */
	IMG2_MIME_HEIF, /**< HEIF: High Efficiency Image Format */
	IMG2_MIME_TIFF, /**< TIFF: Tagged Image File Format */
	IMG2_MIME_RAW, /**< RAW: Camera RAW formats (CR2, NEF, ARW, DNG, etc.) */
	IMG2_MIME_QOI, /**< QOI: Quite OK Image format */
	IMG2_MIME_ICO, /**< ICO: Windows Icon format */
	IMG2_MIME_CUR, /**< CUR: Windows Cursor format */
	IMG2_MIME_JXL, /**< JXL: JPEG XL format */
	IMG2_MIME_SVG, /**< SVG: Scalable Vector Graphics */
	IMG2_MIME_AVIF, /**< AVIF: AV1 Image File Format */
	IMG2_MIME_YUV, /**< Raw YUV (headerless; only via --yuv, never returned by img2_detect_mime) */
} img2_mime_t;

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
 * 5. PNM/PAM ("P1".."P7" followed by a whitespace byte; checked before
 *    TGA because the TGA heuristic can match Netpbm headers)
 * 6. TGA (header-based: byte[2] and byte[16])
 * 7. PSD (4 byte signature "8BPS")
 * 8. HDR (10 or 6 byte signature)
 *
 * @param data Pointer to file data buffer
 * @param len Length of data buffer in bytes
 * @return Detected img2_mime_t, or IMG2_MIME_UNKNOWN if not recognized
 *
 * @note Requires at least 8 bytes of data for reliable detection
 * @note Returns IMG2_MIME_UNKNOWN if len < 8 or no signature matches
 *
 * @example
 * uint8_t* data;
 * size_t size;
 * if (read_file_secure("image.png", &data, &size)) {
 *     img2_mime_t mime = img2_detect_mime(data, size);
 *     if (mime == IMG2_MIME_PNG) {
 *         printf("PNG image detected\n");
 *     }
 *     free(data);
 * }
 */
img2_mime_t img2_detect_mime(const uint8_t *data, size_t len);

/**
 * @brief Get human-readable name for MIME type
 *
 * @param mime MIME type enum value
 * @return String name (e.g., "PNG", "JPEG"), or "UNKNOWN"
 */
const char *img2_mime_name(img2_mime_t mime);

/**
 * @brief Get MIME type string (e.g., "image/png")
 *
 * Converts img2_mime_t enum to standard MIME type string.
 *
 * @param mime MIME type enum
 * @return MIME type string (e.g., "image/png", "image/jpeg")
 */
const char *img2_mime_string(img2_mime_t mime);

#endif /* IMG2_INTERNAL_DECODERS_MAGIC_H */
