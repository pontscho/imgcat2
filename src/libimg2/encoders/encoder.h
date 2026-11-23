/**
 * @file encoder.h
 * @brief Image encoder architecture and registry system
 *
 * Provides a unified encoder API with plugin-style architecture for
 * multiple image output formats. Supports JPEG, PNG, HEIF, WebP, JXL and AVIF
 * encoding with quality/compression control.
 */

#ifndef IMG2_INTERNAL_ENCODERS_ENCODER_H
#define IMG2_INTERNAL_ENCODERS_ENCODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <img2/encode.h>

#include "../core/image.h"

/* Public (<img2/encode.h>): img2_format_t, img2_encode_opts_t, img2_encoder_info_t, img2_encode,
 * img2_can_encode, img2_encoder_count, img2_encoder_info. */

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
 * @note For AVIF: quality 0-100 (aom quality, higher = better quality, larger file)
 */
typedef img2_status_t (*img2_encode_fn)(const img2_image_t *img, int native_quality, uint8_t **out_data, size_t *out_size);

/** @brief Runtime availability check of an encoder row (NULL = always available when built) */
typedef bool (*img2_encoder_available_fn)(void);

typedef struct img2_encoder_entry { img2_encoder_info_t info; img2_encode_fn encode; int default_quality; img2_encoder_available_fn available; } img2_encoder_entry_t;

/** @brief Find a usable encoder by output format: built in and, if the row has a runtime check, available
 *  (silent: a miss is a normal answer) */
const img2_encoder_entry_t *img2_encoder_find(img2_format_t format);

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
img2_status_t img2_encode_avif(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);

/* Runtime availability of the libheif codec plugin (encoder_heif.c): HEVC for HEIF, AV1 for AVIF */
bool img2_encode_heif_available(void);
bool img2_encode_avif_available(void);

#endif /* IMG2_INTERNAL_ENCODERS_ENCODER_H */
