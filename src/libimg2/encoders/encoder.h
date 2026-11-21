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

#include <stdbool.h>
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
 * @param native_quality Quality/compression parameter (format-specific)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return IMG2_OK; IMG2_E_ARG for bad arguments, IMG2_E_NOMEM, IMG2_E_CODEC for library failures
 *
 * @note Caller must free *out_data with free() when done
 * @note For JPEG: quality 0-100 (higher = better quality, larger file)
 * @note For PNG: quality 0-9 (higher = better compression, smaller file, slower)
 * @note For HEIF: quality 0-100 (higher = better quality, larger file)
 * @note For WebP: quality 0-100 (100 = lossless, 0-99 = lossy)
 * @note For JXL: quality 0-100 (>=95 = lossless, 0-94 = lossy)
 */
typedef img2_status_t (*img2_encode_fn)(const img2_image_t *img, int native_quality, uint8_t **out_data, size_t *out_size);

typedef struct img2_encoder_info { img2_format_t format; const char *name; const char *extension; } img2_encoder_info_t;
typedef struct img2_encoder_entry { img2_encoder_info_t info; img2_encode_fn encode; int default_quality; } img2_encoder_entry_t;

/** @brief Number of rows in the const encoder table */
size_t img2_encoder_count(void);

/** @brief Row @p index of the encoder table, NULL if out of range */
const img2_encoder_info_t *img2_encoder_info(size_t index);

/** @brief Find encoder by output format (silent: a miss is a normal answer) */
const img2_encoder_entry_t *img2_encoder_find(img2_format_t format);

/** @brief true if an encoder for @p format is built */
bool img2_can_encode(img2_format_t format);

/** @brief Log the legacy "no encoder" ERROR lines for @p format ([R2]/[P9]) */
void img2_encoder_log_missing(img2_format_t format);

/**
 * @brief Log the encoder table at IMG2_LOG_INFO (the table itself is const)
 */
void img2_encoder_registry_init(void);

/**
 * @brief Encode image data with specified format
 *
 * @param img Input image in RGBA8888 format
 * @param format Output format
 * @param native_quality Quality/compression parameter (format-specific, see img2_encode_fn)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return IMG2_OK; IMG2_E_ARG (bad arguments, IMG2_FORMAT_NONE); IMG2_E_UNSUPPORTED (no encoder built);
 *         else the encoder's code
 */
img2_status_t img2_encoder_dispatch(const img2_image_t *img, img2_format_t format, int native_quality, uint8_t **out_data, size_t *out_size);

/* [R10] Direct encoder entry points (encoder_<x>.c), declared unconditionally. */
img2_status_t img2_encode_jpeg(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
img2_status_t img2_encode_png(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
img2_status_t img2_encode_webp(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
img2_status_t img2_encode_heif(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
img2_status_t img2_encode_jxl(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);

#endif /* IMG2_INTERNAL_ENCODERS_ENCODER_H */
