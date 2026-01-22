/**
 * @file encoder.h
 * @brief Image encoder architecture and registry system
 *
 * Provides a unified encoder API with plugin-style architecture for
 * multiple image output formats. Supports JPEG, PNG, HEIF, WebP, and JXL
 * encoding with quality/compression control.
 */

#ifndef IMGCAT2_ENCODER_H
#define IMGCAT2_ENCODER_H

#include <stddef.h>
#include <stdint.h>

#include "../core/cli.h"
#include "../core/image.h"

/* output_format_t is defined in cli.h */

/**
 * @typedef encode_func_t
 * @brief Encoder function pointer type
 *
 * @param img Input image in RGBA8888 format
 * @param quality Quality/compression parameter (format-specific)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return 0 on success, -1 on error
 *
 * @note Caller must free *out_data with free() when done
 * @note For JPEG: quality 0-100 (higher = better quality, larger file)
 * @note For PNG: quality 0-9 (higher = better compression, smaller file, slower)
 * @note For HEIF: quality 0-100 (higher = better quality, larger file)
 * @note For WebP: quality 0-100 (100 = lossless, 0-99 = lossy)
 * @note For JXL: quality 0-100 (>=95 = lossless, 0-94 = lossy)
 */
typedef int (*encode_func_t)(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);

/**
 * @struct encoder_t
 * @brief Encoder registry entry
 *
 * Represents a single image format encoder with metadata and function pointer.
 */
typedef struct {
	output_format_t format; /**< Output format this encoder handles */
	const char *name; /**< Human-readable format name (e.g., "JPEG", "PNG") */
	const char *extension; /**< File extension (e.g., ".jpg", ".png") */
	encode_func_t encode; /**< Encoder function pointer */
} encoder_t;

/**
 * @brief Global encoder registry
 *
 * Array of all registered encoders. Populated at compile-time based on
 * enabled libraries (HAVE_LIBJPEG, HAVE_LIBPNG, HAVE_HEIF, HAVE_WEBP, HAVE_JXL).
 */
extern const encoder_t *g_encoder_registry;

/**
 * @brief Number of registered encoders
 */
extern size_t g_encoder_count;

/**
 * @brief Initialize encoder registry
 *
 * Populates the global encoder registry with enabled encoders based on
 * compile-time flags (HAVE_LIBJPEG, HAVE_LIBPNG, HAVE_HEIF, HAVE_WEBP, HAVE_JXL).
 *
 * Must be called once at program startup before any encoding operations.
 *
 * @param opts CLI options structure (for logging verbosity)
 *
 * @note This function is idempotent (safe to call multiple times)
 */
void encoder_registry_init(const cli_options_t *opts);

/**
 * @brief Find encoder by output format
 *
 * Looks up an encoder in the registry that can handle the specified format.
 *
 * @param format Output format to find encoder for
 * @return Pointer to encoder_t, or NULL if no encoder found
 *
 * @note Must call encoder_registry_init() before using this function
 *
 * @example
 * const encoder_t* enc = encoder_find_by_format(FORMAT_JPEG);
 * if (enc != NULL) {
 *     printf("Found encoder: %s\n", enc->name);
 * }
 */
const encoder_t *encoder_find_by_format(output_format_t format);

/**
 * @brief Encode image data with specified format
 *
 * Main encoding dispatcher that:
 * 1. Validates inputs
 * 2. Finds appropriate encoder by format
 * 3. Calls encoder function
 * 4. Validates output
 * 5. Returns encoded data buffer
 *
 * @param img Input image in RGBA8888 format
 * @param format Output format (FORMAT_JPEG, FORMAT_PNG, FORMAT_HEIF, FORMAT_WEBP, FORMAT_JXL)
 * @param quality Quality/compression parameter (format-specific, see encode_func_t)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return 0 on success, -1 on error
 *
 * @note Caller must free *out_data with free() when done
 * @note Prints detailed error messages to stderr on failure
 *
 * @example
 * image_t* img = // decoded image
 * uint8_t* encoded_data;
 * size_t encoded_size;
 * if (encoder_encode(img, FORMAT_JPEG, 90, &encoded_data, &encoded_size) == 0) {
 *     // Write encoded_data to file or stdout
 *     free(encoded_data);
 * }
 */
int encoder_encode(const image_t *img, output_format_t format, int quality, uint8_t **out_data, size_t *out_size);

/**
 * @brief Direct encoder function declarations
 *
 * These functions are implemented in encoder_jpeg.c, encoder_png.c, encoder_heif.c,
 * encoder_webp.c, and encoder_jxl.c. Available only if the corresponding library
 * is enabled at compile-time.
 */

#ifdef HAVE_LIBJPEG
/**
 * @brief Encode image to JPEG format
 *
 * @param img Input image in RGBA8888 format
 * @param quality JPEG quality (0-100, higher = better quality)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size
 * @return 0 on success, -1 on error
 */
extern int encode_jpeg(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef HAVE_LIBPNG
/**
 * @brief Encode image to PNG format
 *
 * @param img Input image in RGBA8888 format
 * @param quality PNG compression level (0-9, higher = better compression)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size
 * @return 0 on success, -1 on error
 */
extern int encode_png(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef HAVE_WEBP
/**
 * @brief Encode image to WebP format
 *
 * @param img Input image in RGBA8888 format
 * @param quality WebP quality (0-100, higher = better quality, 100 = lossless)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size
 * @return 0 on success, -1 on error
 */
extern int encode_webp(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef HAVE_HEIF
/**
 * @brief Encode image to HEIF format
 *
 * @param img Input image in RGBA8888 format
 * @param quality HEIF quality (0-100, higher = better quality)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size
 * @return 0 on success, -1 on error
 */
extern int encode_heif(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef HAVE_JXL
/**
 * @brief Encode image to JXL format
 *
 * @param img Input image in RGBA8888 format
 * @param quality JXL quality (0-100, higher = better quality, >=95 = lossless)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size
 * @return 0 on success, -1 on error
 */
extern int encode_jxl(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#endif /* IMGCAT2_ENCODER_H */
