/**
 * @file decoder.h
 * @brief Image decoder architecture and registry system
 *
 * Provides a unified decoder API with plugin-style architecture for
 * multiple image formats. Supports both single-frame (static) and
 * multi-frame (animated) images.
 */

#ifndef IMG2_INTERNAL_DECODERS_DECODER_H
#define IMG2_INTERNAL_DECODERS_DECODER_H

#include <png.h>
#include <stdbool.h>
#include <stddef.h>

#include <img2/decode.h>

#include "../core/image.h"
#include "magic.h"
#include "yuv.h"

/* Public (<img2/decode.h>): img2_decode_opts_t, img2_decoder_info_t, img2_decode, img2_can_decode,
 * img2_is_animated, img2_decoder_count, img2_decoder_info. */

/**
 * @typedef img2_decode_fn
 * @brief Decoder contract
 *
 * On success *out holds count >= 1 frames (loop_count 0). On error *out is
 * zeroed, partial frames are freed and the status says why.
 */
typedef img2_status_t (*img2_decode_fn)(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);

/** @brief Cheap "is this animated" probe */
typedef bool (*img2_probe_fn)(const uint8_t *data, size_t len);

typedef struct img2_decoder_entry { img2_decoder_info_t info; img2_decode_fn decode; img2_probe_fn is_animated; } img2_decoder_entry_t;

/**
 * @brief Find decoder by MIME type (silent: a miss is a normal answer)
 *
 * @return First matching row (priority: native libs > STB fallbacks), or NULL
 */
const img2_decoder_entry_t *img2_decoder_find(img2_mime_t mime);

/**
 * @brief Log the decoder table at IMG2_LOG_INFO (the table itself is const)
 *
 * Decoder priority (native libraries preferred over STB fallbacks):
 * 1. PNG: img2_decode_png (libpng) if IMG2_HAVE_LIBPNG, else img2_decode_stb
 * 2. JPEG: img2_decode_jpeg (libjpeg-turbo) if IMG2_HAVE_LIBJPEG, else img2_decode_stb
 * 3. GIF: img2_decode_gif_animated (giflib) if IMG2_HAVE_GIFLIB, else not supported
 * 4. BMP, TGA, PSD, HDR: img2_decode_stb (stb_image always available)
 * 5. PNM/PAM (P1-P7): img2_decode_pnm (native, always available)
 */
void img2_decoder_registry_init(void);

/**
 * @brief Decode @p data of type @p mime into @p out
 *
 * @param opts Decode options (may be NULL; required with yuv set for IMG2_MIME_YUV)
 * @param data Raw image file data
 * @param len Length of data in bytes
 * @param mime Detected MIME type, or IMG2_MIME_YUV for raw YUV input (geometry from opts->yuv)
 * @param out Output frames (zeroed on error; free with img2_frames_free())
 * @return IMG2_OK; IMG2_E_ARG for bad arguments; IMG2_E_FORMAT for IMG2_MIME_UNKNOWN or invalid
 *         decoder output; IMG2_E_UNSUPPORTED if no decoder is built; else the decoder's code
 */
img2_status_t img2_decoder_dispatch(const img2_decode_opts_t *opts, const uint8_t *data, size_t len, img2_mime_t mime, img2_frames_t *out);

/**
 * @brief Internal: hand a legacy (array, count) decode result over to @p out
 *
 * frames == NULL -> *out zeroed, returns @p fail (IMG2_E_INTERNAL if fail is IMG2_OK).
 */
img2_status_t img2_frames_adopt(img2_frames_t *out, img2_image_t **frames, int count, img2_status_t fail);

/** @brief Internal: set *st (if st) and return NULL (error path of the decoders' frame helpers) */
static inline img2_image_t **img2_decode_fail(img2_status_t *st, img2_status_t code)
{
	if (st != NULL) {
		*st = code;
	}
	return NULL;
}

/**
 * @brief Free multi-frame decoder output
 *
 * Cleans up memory allocated by decoder functions:
 * - Calls img2_image_destroy() for each frame
 * - Frees the frames array itself
 *
 * @param frames Array of img2_image_t* pointers
 * @param frame_count Number of frames in array
 *
 * @note NULL-safe (checks frames != NULL)
 * @note Safe to call with frame_count = 0
 *
 * @example
 * img2_image_t** frames = img2_decoder_dispatch(data, size, mime, &frame_count);
 * if (frames != NULL) {
 *     // Use frames...
 *     img2_decoder_free_frames(frames, frame_count);
 * }
 */
void img2_decoder_free_frames(img2_image_t **frames, int frame_count);

/**
 * @brief Check if GIF is animated (has multiple frames)
 *
 * Quickly checks if a GIF file contains multiple frames without
 * fully decoding the image data.
 *
 * @param data Raw GIF file data
 * @param size Size of data in bytes
 * @return true if GIF has more than one frame, false otherwise or on error
 *
 * @note Returns false if data is invalid or not a GIF
 */
bool img2_gif_is_animated(const uint8_t *data, size_t size);

#ifdef PNG_APNG_SUPPORTED
/**
 * @brief Check if PNG is animated (APNG format)
 *
 * Quickly checks if a PNG file contains animation control chunk (acTL)
 * without fully decoding the image data. Returns false if APNG support
 * is not available at compile time.
 *
 * @param data Raw PNG file data
 * @param size Size of data in bytes
 * @return true if PNG has more than one frame (APNG), false otherwise or on error
 *
 * @note Returns false if PNG_APNG_SUPPORTED not defined
 * @note Returns false if data is invalid or not a PNG
 */
bool img2_png_is_animated(const uint8_t *data, size_t size);
#endif

/**
 * @brief Check if HEIF is an image sequence (has multiple images)
 *
 * Quickly checks if a HEIF file contains multiple top-level images without
 * fully decoding the image data. Useful for determining whether
 * to use static or animated decoder.
 *
 * @param data Raw HEIF file data
 * @param len Size of data in bytes
 * @return true if HEIF has multiple images, false otherwise or on error
 *
 * @note Returns false if data is invalid or not a HEIF
 * @note Does not validate that images are decodable, only checks count
 */
bool img2_heif_is_animated(const uint8_t *data, size_t len);

/**
 * @brief Check if WebP is animated (has multiple frames)
 *
 * Quickly checks if a WebP file contains multiple frames without
 * fully decoding the image data. Useful for determining whether
 * to use static or animated decoder.
 *
 * @param data Raw WebP file data
 * @param len Size of data in bytes
 * @return true if WebP has animation, false otherwise or on error
 *
 * @note Returns false if data is invalid or not a WebP
 * @note Does not validate that frames are decodable, only checks for animation flag
 */
bool img2_webp_is_animated(const uint8_t *data, size_t len);

/**
 * @brief Decode SVG image using nanosvg (fallback decoder)
 *
 * @param data Raw SVG file data
 * @param len Length of data in bytes
 * @param opts Decode options (unused)
 * @param out Output: one frame
 * @return IMG2_OK or an error status
 */
img2_status_t img2_decode_svg_nanosvg(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);

/**
 * @brief Decode SVG image using resvg (preferred decoder)
 *
 * @param data Raw SVG file data
 * @param len Length of data in bytes
 * @param opts Decode options (unused)
 * @param out Output: one frame
 * @return IMG2_OK or an error status
 */
img2_status_t img2_decode_svg_resvg(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);

/* [L6] Every decoder entry point, declared unconditionally (unbuilt ones are simply never referenced). */
img2_status_t img2_decode_png(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
img2_status_t img2_decode_jpeg(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
img2_status_t img2_decode_gif_animated(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
img2_status_t img2_decode_webp(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
img2_status_t img2_decode_heif(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
img2_status_t img2_decode_avif(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
img2_status_t img2_decode_tiff(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
img2_status_t img2_decode_raw(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
img2_status_t img2_decode_jxl(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
img2_status_t img2_decode_qoi(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
img2_status_t img2_decode_ico(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
img2_status_t img2_decode_pnm(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
img2_status_t img2_decode_stb(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);
/** @brief Raw YUV: geometry from opts->yuv (IMG2_E_ARG if NULL); only the first frame is decoded */
img2_status_t img2_decode_yuv(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out);

#endif /* IMG2_INTERNAL_DECODERS_DECODER_H */
