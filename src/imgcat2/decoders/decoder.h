/**
 * @file decoder.h
 * @brief Image decoder architecture and registry system
 *
 * Provides a unified decoder API with plugin-style architecture for
 * multiple image formats. Supports both single-frame (static) and
 * multi-frame (animated) images.
 */

#ifndef IMGCAT2_DECODER_H
#define IMGCAT2_DECODER_H

#include <png.h>
#include <stddef.h>

#include "../core/cli.h"
#include "../core/image.h"
#include "magic.h"

/**
 * @typedef decode_func_t
 * @brief Decoder function pointer type
 *
 * @param data Raw image file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (1 for static, N for animated)
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with decoder_free_frames()
 * @note For static images, frame_count = 1
 * @note For animated images (e.g., GIF), frame_count = N
 */
typedef image_t **(*decode_func_t)(const uint8_t *data, size_t len, int *frame_count);

/**
 * @struct decoder_t
 * @brief Decoder registry entry
 *
 * Represents a single image format decoder with metadata and function pointer.
 */
typedef struct {
	mime_type_t mime_type; /**< MIME type this decoder handles */
	const char *name; /**< Human-readable format name (e.g., "PNG", "JPEG") */
	decode_func_t decode; /**< Decoder function pointer */
} decoder_t;

/**
 * @brief Global decoder registry
 *
 * Array of all registered decoders. Populated at compile-time based on
 * enabled libraries (HAVE_LIBPNG, HAVE_LIBJPEG, etc.).
 */
extern const decoder_t *g_decoder_registry;

/**
 * @brief Number of registered decoders
 */
extern size_t g_decoder_count;

/**
 * @brief Initialize decoder registry
 *
 * Populates the global decoder registry with enabled decoders based on
 * compile-time flags (HAVE_LIBPNG, HAVE_LIBJPEG, etc.).
 *
 * Must be called once at program startup before any decoding operations.
 *
 * Decoder priority (native libraries preferred over STB fallbacks):
 * 1. PNG: decode_png (libpng) if HAVE_LIBPNG, else decode_stb
 * 2. JPEG: decode_jpeg (libjpeg-turbo) if HAVE_LIBJPEG, else decode_stb
 * 3. GIF: decode_gif (giflib) if HAVE_GIFLIB, else not supported
 * 4. BMP, TGA, PSD, HDR, PNM: decode_stb (stb_image always available)
 *
 * @param opts CLI options structure (currently unused, reserved for future use)
 *
 * @note This function is idempotent (safe to call multiple times)
 */
void decoder_registry_init(cli_options_t *opts);

/**
 * @brief Find decoder by MIME type
 *
 * Looks up a decoder in the registry that can handle the specified MIME type.
 *
 * @param mime MIME type to find decoder for
 * @return Pointer to decoder_t, or NULL if no decoder found
 *
 * @note Returns first matching decoder (priority: native libs > STB fallbacks)
 * @note Must call decoder_registry_init() before using this function
 *
 * @example
 * const decoder_t* dec = decoder_find_by_mime(MIME_PNG);
 * if (dec != NULL) {
 *     printf("Found decoder: %s\n", dec->name);
 * }
 */
const decoder_t *decoder_find_by_mime(mime_type_t mime);

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
 * @param opts CLI options structure (for logging verbosity)
 * @param data Raw image file data
 * @param len Length of data in bytes
 * @param mime Detected MIME type (from detect_mime_type())
 * @param frame_count Output: number of frames decoded
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with decoder_free_frames()
 * @note Prints detailed error messages to stderr on failure
 * @note Validates each frame dimensions within IMAGE_MAX_DIMENSION limits
 *
 * @example
 * uint8_t* data;
 * size_t size;
 * if (read_file_secure("image.png", &data, &size)) {
 *     mime_type_t mime = detect_mime_type(data, size);
 *     int frame_count;
 *     image_t** frames = decoder_decode(data, size, mime, &frame_count);
 *     if (frames != NULL) {
 *         // Render frames...
 *         decoder_free_frames(frames, frame_count);
 *     }
 *     free(data);
 * }
 */
image_t **decoder_decode(cli_options_t *opts, const uint8_t *data, size_t len, mime_type_t mime, int *frame_count);

/**
 * @brief Free multi-frame decoder output
 *
 * Cleans up memory allocated by decoder functions:
 * - Calls image_destroy() for each frame
 * - Frees the frames array itself
 *
 * @param frames Array of image_t* pointers
 * @param frame_count Number of frames in array
 *
 * @note NULL-safe (checks frames != NULL)
 * @note Safe to call with frame_count = 0
 *
 * @example
 * image_t** frames = decoder_decode(data, size, mime, &frame_count);
 * if (frames != NULL) {
 *     // Use frames...
 *     decoder_free_frames(frames, frame_count);
 * }
 */
void decoder_free_frames(image_t **frames, int frame_count);

#ifdef HAVE_GIFLIB
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
 * @note Requires HAVE_GIFLIB to be defined
 */
bool gif_is_animated(const uint8_t *data, size_t size);
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
bool png_is_animated(const uint8_t *data, size_t size);
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
bool heif_is_animated(const uint8_t *data, size_t len);

/**
 * @brief Check if AVIF is an image sequence (has multiple images)
 *
 * Quickly checks if an AVIF file contains multiple top-level images without
 * fully decoding the image data. Useful for determining whether
 * to use static or animated decoder.
 *
 * @param data Raw AVIF file data
 * @param len Size of data in bytes
 * @return true if AVIF has multiple images, false otherwise or on error
 *
 * @note Returns false if data is invalid or not an AVIF
 * @note Does not validate that images are decodable, only checks count
 */
bool avif_is_animated(const uint8_t *data, size_t len);

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
bool webp_is_animated(const uint8_t *data, size_t len);

/**
 * @brief Decode SVG image using nanosvg (fallback decoder)
 *
 * @param data Raw SVG file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 */
image_t **decode_svg_nanosvg(const uint8_t *data, size_t len, int *frame_count);

#ifdef HAVE_RESVG
/**
 * @brief Decode SVG image using resvg (preferred decoder)
 *
 * @param data Raw SVG file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 */
image_t **decode_svg_resvg(const uint8_t *data, size_t len, int *frame_count);
#endif

#endif /* IMGCAT2_DECODER_H */
