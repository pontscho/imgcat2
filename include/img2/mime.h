/**
 * @file mime.h
 * @brief libimg2 image type identification (magic bytes)
 */

#ifndef IMG2_MIME_H
#define IMG2_MIME_H

#include <img2/common.h>

IMG2_BEGIN_DECLS

/**
 * @brief Image types (append-only: values are ABI)
 */
typedef enum img2_mime {
	IMG2_MIME_UNKNOWN = 0,    /**< Unknown or unsupported format */
	IMG2_MIME_PNG,            /**< PNG (incl. APNG) */
	IMG2_MIME_JPEG,           /**< JPEG */
	IMG2_MIME_GIF,            /**< GIF */
	IMG2_MIME_BMP,            /**< Windows bitmap */
	IMG2_MIME_TGA,            /**< Truevision Targa */
	IMG2_MIME_PSD,            /**< Adobe Photoshop document */
	IMG2_MIME_HDR,            /**< Radiance RGBE */
	IMG2_MIME_PNM,            /**< Netpbm (PBM/PGM/PPM/PAM) */
	IMG2_MIME_WEBP,           /**< WebP */
	IMG2_MIME_HEIF,           /**< HEIF / HEIC */
	IMG2_MIME_TIFF,           /**< TIFF */
	IMG2_MIME_RAW,            /**< Camera RAW (CR2, NEF, ARW, DNG, ...) */
	IMG2_MIME_QOI,            /**< Quite OK Image format */
	IMG2_MIME_ICO,            /**< Windows icon */
	IMG2_MIME_CUR,            /**< Windows cursor */
	IMG2_MIME_JXL,            /**< JPEG XL */
	IMG2_MIME_SVG,            /**< SVG */
	IMG2_MIME_AVIF,           /**< AVIF */
	IMG2_MIME_YUV             /**< Headerless raw YUV: never detected, only via img2_decode_opts_t.force_mime */
} img2_mime_t;

/**
 * @brief Detect the image type from the leading bytes of a buffer
 *
 * @param buf Data (may be NULL)
 * @param len Length of buf in bytes
 * @return Detected type, or IMG2_MIME_UNKNOWN (never IMG2_MIME_YUV)
 */
img2_mime_t img2_detect_mime(const uint8_t *buf, size_t len);

/**
 * @brief Short name of a type
 *
 * @param mime Type
 * @return Static string, e.g. "PNG" ("UNKNOWN" for unknown values)
 */
const char *img2_mime_name(img2_mime_t mime);

/**
 * @brief MIME string of a type
 *
 * @param mime Type
 * @return Static string, e.g. "image/png"
 */
const char *img2_mime_string(img2_mime_t mime);

IMG2_END_DECLS

#endif /* IMG2_MIME_H */
