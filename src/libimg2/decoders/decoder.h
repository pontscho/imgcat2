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

#include <limits.h>
#include <png.h>
#include <stdbool.h>
#include <stddef.h>

#include <img2/decode.h>

#include "../core/image.h"
#include "../core/log.h"
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
 * @brief Internal: allocate a decoder's first frame / canvas with the LIMIT vs NOMEM decision
 *
 * @param out Allocated image, NULL on error
 * @return IMG2_OK; IMG2_E_LIMIT if the dimensions fail img2_image_calc_size() (zero, > IMG2_MAX_DIMENSION,
 *         > IMG2_MAX_PIXELS); IMG2_E_NOMEM if the allocation failed
 */
static inline img2_status_t img2_decode_image_alloc(uint32_t width, uint32_t height, img2_image_t **out)
{
	size_t byte_count;
	bool size_ok = img2_image_calc_size(width, height, &byte_count);

	*out = img2_image_alloc(width, height);
	if (*out == NULL) {
		return size_ok ? IMG2_E_NOMEM : IMG2_E_LIMIT;
	}
	return IMG2_OK;
}

/**
 * @brief Internal [P33]: reject a header-announced size before the codec allocates for it
 *
 * Codecs (libjpeg buffered mode, qoi.h, stb_image, libwebp, libheif, LibRaw) allocate image-sized buffers
 * before the decoder sees the decoded size: the header's size is checked against img2_image_calc_size() first.
 *
 * @param fmt Format name of the log line
 * @return IMG2_E_LIMIT (logged) if width and height are both > 0 and fail img2_image_calc_size(); else IMG2_OK
 *         (a size <= 0 is left to the codec, which reports it as before)
 */
static inline img2_status_t img2_decode_check_header_size(const char *fmt, int64_t width, int64_t height)
{
	size_t byte_count;

	if (width <= 0 || height <= 0 || (width <= IMG2_MAX_DIMENSION && height <= IMG2_MAX_DIMENSION && img2_image_calc_size((uint32_t)width, (uint32_t)height, &byte_count))) {
		return IMG2_OK;
	}
	img2_log(IMG2_LOG_ERROR, "Error: %s dimensions %lldx%lld exceed the limits (%u per side, %lu pixels)", fmt, (long long)width, (long long)height, (unsigned)IMG2_MAX_DIMENSION, (unsigned long)IMG2_MAX_PIXELS);
	return IMG2_E_LIMIT;
}

/**
 * @brief Internal [F14]: add one frame of width x height to the running pixel total of an animated decode
 *
 * @return IMG2_OK, or IMG2_E_LIMIT if the total would exceed IMG2_MAX_TOTAL_PIXELS (total unchanged then)
 */
static inline img2_status_t img2_decode_add_frame_pixels(uint64_t *total, uint32_t width, uint32_t height)
{
	uint64_t pixels = (uint64_t)width * (uint64_t)height;

	if (pixels > IMG2_MAX_TOTAL_PIXELS || *total > IMG2_MAX_TOTAL_PIXELS - pixels) {
		img2_log(IMG2_LOG_ERROR, "Error: Animation exceeds the total frame pixel limit (%llu)", (unsigned long long)IMG2_MAX_TOTAL_PIXELS);
		return IMG2_E_LIMIT;
	}
	*total += pixels;
	return IMG2_OK;
}

/**
 * @brief Internal [F14]: fail fast before any allocation when frame_count full-canvas frames cannot fit
 *        into IMG2_MAX_TOTAL_PIXELS (the running total in the frame loop stays the authoritative check)
 *
 * @return IMG2_OK or IMG2_E_LIMIT
 */
static inline img2_status_t img2_decode_check_total_pixels(uint64_t frame_count, uint32_t width, uint32_t height)
{
	uint64_t pixels = (uint64_t)width * (uint64_t)height;

	if (pixels != 0 && frame_count > IMG2_MAX_TOTAL_PIXELS / pixels) {
		img2_log(IMG2_LOG_ERROR, "Error: Animation exceeds the total frame pixel limit (%llu)", (unsigned long long)IMG2_MAX_TOTAL_PIXELS);
		return IMG2_E_LIMIT;
	}
	return IMG2_OK;
}

/**
 * @brief Internal [F17]: a duration of @p ticks at @p tps_num / @p tps_den ticks per second, in milliseconds
 *
 * @return floor(ticks * 1000 * tps_den / tps_num) saturated at UINT32_MAX; 0 if tps_num is 0
 */
static inline uint32_t img2_decode_ticks_to_ms(uint32_t ticks, uint32_t tps_num, uint32_t tps_den)
{
	if (tps_num == 0) {
		return 0;
	}
	/* ticks * tps_den fits in 64 bits, the factor 1000 may not: split the quotient, q * 1000 + r * 1000 / tps_num
	 * (r < tps_num < 2^32, so r * 1000 fits) */
	uint64_t scaled = (uint64_t)ticks * tps_den;
	uint64_t q = scaled / tps_num;
	uint64_t r = scaled % tps_num;
	if (q > UINT32_MAX / 1000u) {
		return UINT32_MAX;
	}
	uint64_t ms = q * 1000u + r * 1000u / tps_num;
	return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
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
 * @brief Check if HEIF/AVIF is animated (image list or image sequence track with more than one frame)
 *
 * True for more than one top-level image, or (with at most one top-level image) for a visual sequence
 * track with at least two samples (counted without decoding). Used as the probe of both HEIF and AVIF.
 *
 * @param data Raw HEIF/AVIF file data
 * @param len Size of data in bytes
 * @return true if animated, false otherwise or on error
 *
 * @note Does not validate that images are decodable
 */
bool img2_heif_is_animated(const uint8_t *data, size_t len);

/** @brief Internal (decoder_heif.c, shared with decoder_avif.c): how a HEIF/AVIF file is decoded */
typedef enum {
	IMG2_HEIF_LAYOUT_SINGLE = 0, /**< primary image (also: unreadable file; the still decoder reports the error) */
	IMG2_HEIF_LAYOUT_LIST,       /**< more than one top-level image: one frame per image, no timing */
	IMG2_HEIF_LAYOUT_SEQUENCE    /**< visual sequence track (no top-level image, or one plus >= 2 track samples) */
} img2_heif_layout_t;

/**
 * @brief Internal: classify a HEIF/AVIF file (see img2_heif_layout_t)
 *
 * @param frames_hint Optional output: top-level image count (LIST), track samples counted up to 2 (SEQUENCE), 1 (SINGLE)
 */
img2_heif_layout_t img2_heif_layout(const uint8_t *data, size_t len, int *frames_hint);

/**
 * @brief Internal: decode the first visual sequence track of a HEIF/AVIF file (libheif track API)
 *
 * Frames are decoded once (edit-list repetition ignored) until end of sequence or the 200-frame cap
 * (WARN, not an error). delay_ms = sample duration in the track timescale, rounded to ms; loop_count from
 * the track's repetition count (see docs/libimg2.md "Animation timing"). Single-frame result: delay 0, loop 0.
 *
 * @param fmt Format name for log lines ("HEIF" / "AVIF")
 * @param loop_count Output: 0 = forever / unknown, N = play N times
 * @return Frames array (count in *frame_count), or NULL with *st set
 */
img2_image_t **img2_heif_decode_sequence(const uint8_t *data, size_t len, const char *fmt, int *frame_count, int *loop_count, img2_status_t *st);

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

/* [F5] stb_image and qoi.h take the input length as int (decoder_stb.c / decoder_qoi.c pass (int)len): the
 * dispatcher rejects len > IMG2_MAX_FILE_SIZE first, so that limit must fit in an int */
_Static_assert(IMG2_MAX_FILE_SIZE <= INT_MAX, "IMG2_MAX_FILE_SIZE must fit the int length of stb_image / qoi.h");

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
