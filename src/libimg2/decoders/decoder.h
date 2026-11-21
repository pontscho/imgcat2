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
#include <stddef.h>

#include "../core/image.h"
#include "magic.h"
#include "yuv.h"

/**
 * @typedef img2_decode_fn
 * @brief Decoder function pointer type
 *
 * @param data Raw image file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (1 for static, N for animated)
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with img2_decoder_free_frames()
 * @note For static images, frame_count = 1
 * @note For animated images (e.g., GIF), frame_count = N
 */
typedef img2_image_t **(*img2_decode_fn)(const uint8_t *data, size_t len, int *frame_count);

/**
 * @struct img2_decoder_entry_t
 * @brief Decoder registry entry
 *
 * Represents a single image format decoder with metadata and function pointer.
 */
typedef struct img2_decode_opts {
	img2_mime_t force_mime;          /* IMG2_MIME_UNKNOWN = detect */
	const img2_yuv_params_t *yuv;         /* required for IMG2_MIME_YUV */
	int max_frames;                  /* <= 0: no extra limit (implemented in S6) */
	int jpeg_deblock;                /* -1 default (jpegqs if built), 0 off, 1 on (implemented in S6) */
} img2_decode_opts_t;

typedef struct {
	img2_mime_t mime_type; /**< MIME type this decoder handles */
	const char *name; /**< Human-readable format name (e.g., "PNG", "JPEG") */
	img2_decode_fn decode; /**< Decoder function pointer */
} img2_decoder_entry_t;

/**
 * @brief Global decoder registry
 *
 * Array of all registered decoders. Populated at compile-time based on
 * enabled libraries (IMG2_HAVE_LIBPNG, IMG2_HAVE_LIBJPEG, etc.).
 */
extern const img2_decoder_entry_t *img2_decoder_registry;

/**
 * @brief Number of registered decoders
 */
extern size_t img2_decoder_registry_size;

/**
 * @brief Initialize decoder registry
 *
 * Populates the global decoder registry with enabled decoders based on
 * compile-time flags (IMG2_HAVE_LIBPNG, IMG2_HAVE_LIBJPEG, etc.).
 *
 * Must be called once at program startup before any decoding operations.
 *
 * Decoder priority (native libraries preferred over STB fallbacks):
 * 1. PNG: img2_decode_png (libpng) if IMG2_HAVE_LIBPNG, else img2_decode_stb
 * 2. JPEG: img2_decode_jpeg (libjpeg-turbo) if IMG2_HAVE_LIBJPEG, else img2_decode_stb
 * 3. GIF: img2_decode_gif_animated (giflib) if IMG2_HAVE_GIFLIB, else not supported
 * 4. BMP, TGA, PSD, HDR: img2_decode_stb (stb_image always available)
 * 5. PNM/PAM (P1-P7): img2_decode_pnm (native, always available)
 *
 * Logs the registered decoders at IMG2_LOG_INFO.
 *
 * @note This function is idempotent (safe to call multiple times)
 */
void img2_decoder_registry_init(void);

/**
 * @brief Find decoder by MIME type
 *
 * Looks up a decoder in the registry that can handle the specified MIME type.
 *
 * @param mime MIME type to find decoder for
 * @return Pointer to img2_decoder_entry_t, or NULL if no decoder found
 *
 * @note Returns first matching decoder (priority: native libs > STB fallbacks)
 * @note Must call img2_decoder_registry_init() before using this function
 *
 * @example
 * const img2_decoder_entry_t* dec = img2_decoder_find(IMG2_MIME_PNG);
 * if (dec != NULL) {
 *     printf("Found decoder: %s\n", dec->name);
 * }
 */
const img2_decoder_entry_t *img2_decoder_find(img2_mime_t mime);

/**
 * @brief Decode image data with automatic format detection
 *
 * Main decoding dispatcher that:
 * 1. Finds appropriate decoder by MIME type
 * 2. Validates decoder exists
 * 3. Calls decoder function
 * 4. Validates output frames
 * 5. Returns decoded frames array
 *
 * @param opts Decode options (may be NULL; required with yuv set for IMG2_MIME_YUV)
 * @param data Raw image file data
 * @param len Length of data in bytes
 * @param mime Detected MIME type (from img2_detect_mime()), or IMG2_MIME_YUV for raw
 *             YUV input (requires opts->yuv; geometry taken from opts->yuv)
 * @param frame_count Output: number of frames decoded
 * @return Array of img2_image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with img2_decoder_free_frames()
 * @note Prints detailed error messages to stderr on failure
 * @note Validates each frame dimensions within IMG2_MAX_DIMENSION limits
 *
 * @example
 * uint8_t* data;
 * size_t size;
 * if (read_file_secure("image.png", &data, &size)) {
 *     img2_mime_t mime = img2_detect_mime(data, size);
 *     int frame_count;
 *     img2_image_t** frames = img2_decoder_dispatch(data, size, mime, &frame_count);
 *     if (frames != NULL) {
 *         // Render frames...
 *         img2_decoder_free_frames(frames, frame_count);
 *     }
 *     free(data);
 * }
 */
img2_image_t **img2_decoder_dispatch(const img2_decode_opts_t *opts, const uint8_t *data, size_t len, img2_mime_t mime, int *frame_count);

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

#ifdef IMG2_HAVE_GIFLIB
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
 * @note Requires IMG2_HAVE_GIFLIB to be defined
 */
bool img2_gif_is_animated(const uint8_t *data, size_t size);
#endif

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
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single img2_image_t*, or NULL on error
 */
img2_image_t **img2_decode_svg_nanosvg(const uint8_t *data, size_t len, int *frame_count);

#ifdef IMG2_HAVE_RESVG
/**
 * @brief Decode SVG image using resvg (preferred decoder)
 *
 * @param data Raw SVG file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single img2_image_t*, or NULL on error
 */
img2_image_t **img2_decode_svg_resvg(const uint8_t *data, size_t len, int *frame_count);
#endif

#endif /* IMG2_INTERNAL_DECODERS_DECODER_H */
