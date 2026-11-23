/**
 * @file encoder.c
 * @brief Image encoder registry and dispatch implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "encoder.h"
#include "../core/log.h"

/**
 * @brief Static encoder table
 *
 * Populated at compile-time based on IMG2_HAVE_* preprocessor flags.
 * default_quality = the CLI defaults (native scale of each encoder).
 * available = runtime check of the codec (libheif plugins: a host libheif may lack kvazaar or aom), NULL = always.
 */
static const img2_encoder_entry_t s_encoders[] = {
#ifdef IMG2_HAVE_LIBJPEG
	{ { IMG2_FORMAT_JPEG, "JPEG (libjpeg)",    ".jpg"  }, img2_encode_jpeg_opts, 90, NULL                        },
#endif

#ifdef IMG2_HAVE_LIBPNG
	{ { IMG2_FORMAT_PNG,  "PNG (libpng)",      ".png"  }, img2_encode_png_opts,  6,  NULL                        },
#endif

#ifdef IMG2_HAVE_HEIF
	{ { IMG2_FORMAT_HEIF, "HEIF (libheif)",    ".heif" }, img2_encode_heif_opts, 80, img2_encode_heif_available },
#endif

#ifdef IMG2_HAVE_WEBP
	{ { IMG2_FORMAT_WEBP, "WebP (libwebp)",    ".webp" }, img2_encode_webp_opts, 80, NULL                        },
#endif

#ifdef IMG2_HAVE_JXL
	{ { IMG2_FORMAT_JXL,  "JXL (libjxl)",      ".jxl"  }, img2_encode_jxl_opts,  80, NULL                        },
#endif

#ifdef IMG2_HAVE_HEIF
	{ { IMG2_FORMAT_AVIF, "AVIF (libheif/aom)", ".avif" }, img2_encode_avif_opts, 80, img2_encode_avif_available },
#endif
};

#define ENCODER_COUNT (sizeof(s_encoders) / sizeof(s_encoders[0]))

size_t img2_encoder_count(void)
{
	return ENCODER_COUNT;
}

const img2_encoder_info_t *img2_encoder_info(size_t index)
{
	return index < ENCODER_COUNT ? &s_encoders[index].info : NULL;
}

/**
 * @brief Log the encoder table (the table is const; nothing to initialise)
 */
void img2_encoder_registry_init(void)
{
	img2_log(IMG2_LOG_INFO, "Encoder registry initialized with %zu encoders:", ENCODER_COUNT);
	for (size_t i = 0; i < ENCODER_COUNT; i++) {
		img2_log(IMG2_LOG_INFO, "  [%zu] %s (%s)", i, s_encoders[i].info.name, s_encoders[i].info.extension);
	}
}

/**
 * @brief Find encoder by output format ([R2] silent)
 */
const img2_encoder_entry_t *img2_encoder_find(img2_format_t format)
{
	for (size_t i = 0; i < ENCODER_COUNT; i++) {
		if (s_encoders[i].info.format == format) {
			if (s_encoders[i].available != NULL && !s_encoders[i].available()) {
				return NULL;
			}
			return &s_encoders[i];
		}
	}
	return NULL;
}

bool img2_can_encode(img2_format_t format)
{
	return img2_encoder_find(format) != NULL;
}

void img2_encoder_log_missing(img2_format_t format)
{
	const char *format_name = (format == IMG2_FORMAT_JPEG) ? "JPEG" : (format == IMG2_FORMAT_PNG) ? "PNG" : (format == IMG2_FORMAT_HEIF) ? "HEIF" : (format == IMG2_FORMAT_WEBP) ? "WebP" : (format == IMG2_FORMAT_JXL) ? "JXL" : (format == IMG2_FORMAT_AVIF) ? "AVIF" : "UNKNOWN";
	img2_log(IMG2_LOG_ERROR, "Error: No encoder found for format: %s", format_name);
	img2_log(IMG2_LOG_ERROR, "Error: No encoder available for format");
}

const img2_encode_opts_t *img2_encode_opts_or_default(const img2_encode_opts_t *opts)
{
	static const img2_encode_opts_t s_default = IMG2_ENCODE_OPTS_INIT;
	return opts != NULL ? opts : &s_default;
}

const char *img2_chroma_name(img2_chroma_t chroma)
{
	switch (chroma) {
		case IMG2_CHROMA_420: return "420";
		case IMG2_CHROMA_422: return "422";
		case IMG2_CHROMA_444: return "444";
		default: return "default";
	}
}

void img2_encode_warn_ignored(const char *option, const char *format, const char *reason)
{
	img2_log(IMG2_LOG_WARN, "encode: %s has no effect for %s (%s)", option, format, reason);
}

img2_status_t img2_encoder_dispatch(const img2_image_t *img, img2_format_t format, int native_quality, uint8_t **out_data, size_t *out_size)
{
	return img2_encoder_dispatch_opts(img, format, native_quality, NULL, out_data, out_size);
}

/**
 * @brief Encode image data with specified format and options
 *
 * Main encoding dispatcher with comprehensive error checking and validation.
 */
img2_status_t img2_encoder_dispatch_opts(const img2_image_t *img, img2_format_t format, int native_quality, const img2_encode_opts_t *opts, uint8_t **out_data, size_t *out_size)
{
	/* Validate inputs */
	if (img == NULL || out_data == NULL || out_size == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to encoder_encode");
		return IMG2_E_ARG;
	}

	/* Initialize outputs */
	*out_data = NULL;
	*out_size = 0;

	/* Validate format */
	if (format == IMG2_FORMAT_NONE) {
		img2_log(IMG2_LOG_ERROR, "Error: No output format specified");
		return IMG2_E_ARG;
	}

	/* Find appropriate encoder */
	const img2_encoder_entry_t *encoder = img2_encoder_find(format);
	if (encoder == NULL) {
		img2_encoder_log_missing(format);
		return IMG2_E_UNSUPPORTED;
	}

	/* Call encoder function */
	img2_status_t result = encoder->encode(img, native_quality, opts, out_data, out_size);
	if (result != IMG2_OK) {
		img2_log(IMG2_LOG_ERROR, "Error: Encoder '%s' failed to encode image", encoder->info.name);
		return result;
	}

	/* Validate output */
	if (*out_data == NULL || *out_size == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: Encoder returned invalid output (data=%p, size=%zu)", (void *)*out_data, *out_size);
		if (*out_data != NULL) {
			free(*out_data);
			*out_data = NULL;
		}
		*out_size = 0;
		return IMG2_E_CODEC;
	}

	return IMG2_OK;
}
