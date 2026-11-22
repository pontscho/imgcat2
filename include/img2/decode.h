/**
 * @file decode.h
 * @brief libimg2 decoding: buffer -> RGBA frames
 */

#ifndef IMG2_DECODE_H
#define IMG2_DECODE_H

#include <img2/common.h>
#include <img2/image.h>
#include <img2/mime.h>
#include <img2/yuv.h>

IMG2_BEGIN_DECLS

/**
 * @brief Decode options (initialise with IMG2_DECODE_OPTS_INIT)
 */
typedef struct img2_decode_opts {
	img2_mime_t force_mime;          /**< IMG2_MIME_UNKNOWN = detect from content */
	const img2_yuv_params_t *yuv;    /**< Required when force_mime == IMG2_MIME_YUV */
	int max_frames;                  /**< <= 0: no extra limit; N > 0: keep the first N frames. Applied after
	                                      decoding (the decoder still decodes every frame it supports), so it
	                                      trims the result and does not bound decode time or memory */
	int jpeg_deblock;                /**< -1 = default (quantsmooth when built), 0 = off, 1 = on */
} img2_decode_opts_t;

/** Default decode options. NOTE: a zeroed struct means jpeg_deblock = off */
#define IMG2_DECODE_OPTS_INIT { IMG2_MIME_UNKNOWN, NULL, 0, -1 }

/**
 * @brief Decode an image buffer
 *
 * buf is untrusted: buf NULL / len 0 / out NULL -> IMG2_E_ARG; len > IMG2_MAX_FILE_SIZE ->
 * IMG2_E_LIMIT; every frame is limited to IMG2_MAX_DIMENSION / IMG2_MAX_PIXELS (else IMG2_E_LIMIT),
 * and the frames of an animated GIF, APNG, WebP, JPEG XL or HEIF/AVIF image list to 2^30 pixels in total
 * (else IMG2_E_LIMIT). Animations longer than 200 frames are clamped to their first 200 frames.
 *
 * @param buf  Encoded image data
 * @param len  Length of buf in bytes
 * @param opts Options, or NULL for defaults
 * @param out  On success owns the frames (free with img2_frames_free()); zeroed on error
 * @return IMG2_OK; IMG2_E_ARG; IMG2_E_LIMIT; IMG2_E_FORMAT (unrecognised or malformed data);
 *         IMG2_E_UNSUPPORTED (no decoder built for the type); IMG2_E_NOMEM; IMG2_E_CODEC
 */
img2_status_t img2_decode(const uint8_t *buf, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);

/**
 * @brief Whether a decoder for a type is built into this library
 *
 * @param mime Type
 * @return true if img2_decode() can decode it (IMG2_MIME_YUV is always true)
 */
bool img2_can_decode(img2_mime_t mime);

/**
 * @brief Cheap "is this an animation" probe (GIF, APNG, animated WebP, HEIF/AVIF sequences)
 *
 * @param buf Encoded image data
 * @param len Length of buf in bytes
 * @return true if animated; false for still images, unknown types, NULL/0/oversized input
 */
bool img2_is_animated(const uint8_t *buf, size_t len);

/**
 * @brief One row of the decoder table
 */
typedef struct img2_decoder_info {
	img2_mime_t mime;    /**< Decoded type */
	const char *name;    /**< Human-readable decoder name, e.g. "PNG (libpng)" */
} img2_decoder_info_t;

/**
 * @brief Number of rows in the decoder table
 *
 * @return Row count
 */
size_t img2_decoder_count(void);

/**
 * @brief Row of the decoder table
 *
 * @param index Row index
 * @return Static row, or NULL if index >= img2_decoder_count()
 */
const img2_decoder_info_t *img2_decoder_info(size_t index);

IMG2_END_DECLS

#endif /* IMG2_DECODE_H */
