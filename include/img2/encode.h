/**
 * @file encode.h
 * @brief libimg2 encoding: RGBA image -> JPEG / PNG / HEIF / WebP / JXL / AVIF buffer
 */

#ifndef IMG2_ENCODE_H
#define IMG2_ENCODE_H

#include <img2/common.h>
#include <img2/image.h>

IMG2_BEGIN_DECLS

/**
 * @brief Output formats
 */
typedef enum img2_format {
	IMG2_FORMAT_NONE = 0,     /**< No format selected */
	IMG2_FORMAT_JPEG,         /**< JPEG */
	IMG2_FORMAT_PNG,          /**< PNG */
	IMG2_FORMAT_HEIF,         /**< HEIF */
	IMG2_FORMAT_WEBP,         /**< WebP */
	IMG2_FORMAT_JXL,          /**< JPEG XL */
	IMG2_FORMAT_AVIF          /**< AVIF (AV1 in HEIF; appended, existing values unchanged) */
} img2_format_t;

/**
 * @brief Encode options (initialise with IMG2_ENCODE_OPTS_INIT)
 *
 * quality: -1 = format default; otherwise 0..100 with per-format meaning (kept from imgcat2 1.0):
 * JPEG 0..100 (default 90); PNG zlib level = quality/10 clamped 0..9 (default 6, always lossless);
 * HEIF 0..100 (default 80); WebP 0..99 lossy, 100 lossless (default 80);
 * JXL 0..94 lossy (distance (100-q)/10), >= 95 lossless (default 80);
 * AVIF 0..100 aom quality, lossy 4:2:0 (default 80).
 */
typedef struct img2_encode_opts {
	int quality;    /**< -1 = default, else 0..100 (see above) */
} img2_encode_opts_t;

/** Default encode options */
#define IMG2_ENCODE_OPTS_INIT { -1 }

/**
 * @brief Encode in->frames[0]
 *
 * @param in      Frame set (count >= 1; every frame must be a valid image, stride == width * 4)
 * @param fmt     Output format
 * @param opts    Options, or NULL for defaults
 * @param out_buf Output buffer, freed with img2_free(); NULL on error
 * @param out_len Output length in bytes; 0 on error
 * @return IMG2_OK; IMG2_E_ARG (invalid arguments, IMG2_FORMAT_NONE); IMG2_E_UNSUPPORTED (encoder not
 *         built); IMG2_E_NOMEM; IMG2_E_CODEC
 */
img2_status_t img2_encode(const img2_frames_t *in, img2_format_t fmt, const img2_encode_opts_t *opts,
                          uint8_t **out_buf, size_t *out_len);

/**
 * @brief Whether an encoder for a format is built into this library and usable at runtime
 *
 * HEIF and AVIF also ask libheif for an HEVC / AV1 encoder plugin, so a host libheif without kvazaar/x265
 * or aom reports false although the table row exists.
 *
 * @param fmt Format
 * @return true if img2_encode() supports it
 */
bool img2_can_encode(img2_format_t fmt);

/**
 * @brief One row of the encoder table
 */
typedef struct img2_encoder_info {
	img2_format_t format;     /**< Output format */
	const char *name;         /**< Human-readable encoder name, e.g. "PNG (libpng)" */
	const char *extension;    /**< File extension incl. the dot, e.g. ".png" */
} img2_encoder_info_t;

/**
 * @brief Number of rows in the encoder table
 *
 * @return Row count
 */
size_t img2_encoder_count(void);

/**
 * @brief Row of the encoder table
 *
 * @param index Row index
 * @return Static row, or NULL if index >= img2_encoder_count()
 */
const img2_encoder_info_t *img2_encoder_info(size_t index);

IMG2_END_DECLS

#endif /* IMG2_ENCODE_H */
