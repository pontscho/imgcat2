/**
 * @file encoder.h
 * @brief Image encoder architecture and registry system
 *
 * Provides a unified encoder API with plugin-style architecture for
 * multiple image output formats. Supports JPEG, PNG, HEIF, WebP, and JXL
 * encoding with quality/compression control.
 */

#ifndef IMG2_INTERNAL_ENCODERS_ENCODER_H
#define IMG2_INTERNAL_ENCODERS_ENCODER_H

#include <stddef.h>
#include <stdint.h>

#include "../core/image.h"

/**
 * @enum img2_format_t
 * @brief Supported output image formats
 */
typedef enum {
	IMG2_FORMAT_NONE = 0, /**< No format selected */
	IMG2_FORMAT_JPEG, /**< JPEG format */
	IMG2_FORMAT_PNG, /**< PNG format */
	IMG2_FORMAT_HEIF, /**< HEIF format */
	IMG2_FORMAT_WEBP, /**< WebP format */
	IMG2_FORMAT_JXL, /**< JXL format */
} img2_format_t;

/**
 * @typedef img2_encode_fn
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
typedef int (*img2_encode_fn)(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);

/**
 * @struct img2_encoder_entry_t
 * @brief Encoder registry entry
 *
 * Represents a single image format encoder with metadata and function pointer.
 */
typedef struct {
	img2_format_t format; /**< Output format this encoder handles */
	const char *name; /**< Human-readable format name (e.g., "JPEG", "PNG") */
	const char *extension; /**< File extension (e.g., ".jpg", ".png") */
	img2_encode_fn encode; /**< Encoder function pointer */
} img2_encoder_entry_t;

/**
 * @brief Global encoder registry
 *
 * Array of all registered encoders. Populated at compile-time based on
 * enabled libraries (IMG2_HAVE_LIBJPEG, IMG2_HAVE_LIBPNG, IMG2_HAVE_HEIF, IMG2_HAVE_WEBP, IMG2_HAVE_JXL).
 */
extern const img2_encoder_entry_t *img2_encoder_registry;

/**
 * @brief Number of registered encoders
 */
extern size_t img2_encoder_registry_size;

/**
 * @brief Initialize encoder registry
 *
 * Populates the global encoder registry with enabled encoders based on
 * compile-time flags (IMG2_HAVE_LIBJPEG, IMG2_HAVE_LIBPNG, IMG2_HAVE_HEIF, IMG2_HAVE_WEBP, IMG2_HAVE_JXL).
 *
 * Must be called once at program startup before any encoding operations.
 *
 * Logs the registered encoders at IMG2_LOG_INFO.
 *
 * @note This function is idempotent (safe to call multiple times)
 */
void img2_encoder_registry_init(void);

/**
 * @brief Find encoder by output format
 *
 * Looks up an encoder in the registry that can handle the specified format.
 *
 * @param format Output format to find encoder for
 * @return Pointer to img2_encoder_entry_t, or NULL if no encoder found
 *
 * @note Must call img2_encoder_registry_init() before using this function
 *
 * @example
 * const img2_encoder_entry_t* enc = img2_encoder_find(IMG2_FORMAT_JPEG);
 * if (enc != NULL) {
 *     printf("Found encoder: %s\n", enc->name);
 * }
 */
const img2_encoder_entry_t *img2_encoder_find(img2_format_t format);

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
 * @param format Output format (IMG2_FORMAT_JPEG, IMG2_FORMAT_PNG, IMG2_FORMAT_HEIF, IMG2_FORMAT_WEBP, IMG2_FORMAT_JXL)
 * @param quality Quality/compression parameter (format-specific, see img2_encode_fn)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return 0 on success, -1 on error
 *
 * @note Caller must free *out_data with free() when done
 * @note Prints detailed error messages to stderr on failure
 *
 * @example
 * img2_image_t* img = // decoded image
 * uint8_t* encoded_data;
 * size_t encoded_size;
 * if (img2_encoder_dispatch(img, IMG2_FORMAT_JPEG, 90, &encoded_data, &encoded_size) == 0) {
 *     // Write encoded_data to file or stdout
 *     free(encoded_data);
 * }
 */
int img2_encoder_dispatch(const img2_image_t *img, img2_format_t format, int quality, uint8_t **out_data, size_t *out_size);

/**
 * @brief Direct encoder function declarations
 *
 * These functions are implemented in encoder_jpeg.c, encoder_png.c, encoder_heif.c,
 * encoder_webp.c, and encoder_jxl.c. Available only if the corresponding library
 * is enabled at compile-time.
 */

#ifdef IMG2_HAVE_LIBJPEG
/**
 * @brief Encode image to JPEG format
 *
 * @param img Input image in RGBA8888 format
 * @param quality JPEG quality (0-100, higher = better quality)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size
 * @return 0 on success, -1 on error
 */
extern int img2_encode_jpeg(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef IMG2_HAVE_LIBPNG
/**
 * @brief Encode image to PNG format
 *
 * @param img Input image in RGBA8888 format
 * @param quality PNG compression level (0-9, higher = better compression)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size
 * @return 0 on success, -1 on error
 */
extern int img2_encode_png(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef IMG2_HAVE_WEBP
/**
 * @brief Encode image to WebP format
 *
 * @param img Input image in RGBA8888 format
 * @param quality WebP quality (0-100, higher = better quality, 100 = lossless)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size
 * @return 0 on success, -1 on error
 */
extern int img2_encode_webp(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef IMG2_HAVE_HEIF
/**
 * @brief Encode image to HEIF format
 *
 * @param img Input image in RGBA8888 format
 * @param quality HEIF quality (0-100, higher = better quality)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size
 * @return 0 on success, -1 on error
 */
extern int img2_encode_heif(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef IMG2_HAVE_JXL
/**
 * @brief Encode image to JXL format
 *
 * @param img Input image in RGBA8888 format
 * @param quality JXL quality (0-100, higher = better quality, >=95 = lossless)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size
 * @return 0 on success, -1 on error
 */
extern int img2_encode_jxl(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#endif /* IMG2_INTERNAL_ENCODERS_ENCODER_H */
