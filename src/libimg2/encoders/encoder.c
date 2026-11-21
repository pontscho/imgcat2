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
 * @brief Forward declarations for encoder functions
 *
 * These will be implemented in separate encoder implementation files
 * (encoder_jpeg.c, encoder_png.c). For now, we declare them
 * to satisfy the registry structure.
 */

#ifdef IMG2_HAVE_LIBJPEG
extern int img2_encode_jpeg(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef IMG2_HAVE_LIBPNG
extern int img2_encode_png(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef IMG2_HAVE_WEBP
extern int img2_encode_webp(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef IMG2_HAVE_HEIF
extern int img2_encode_heif(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef IMG2_HAVE_JXL
extern int img2_encode_jxl(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

/**
 * @brief Static encoder registry array
 *
 * Populated at compile-time based on HAVE_* preprocessor flags.
 * Conditional compilation ensures only available encoders are registered.
 */
static const img2_encoder_entry_t s_encoder_registry[] = {
#ifdef IMG2_HAVE_LIBJPEG
	{ IMG2_FORMAT_JPEG, "JPEG (libjpeg)", ".jpg",  img2_encode_jpeg },
#endif

#ifdef IMG2_HAVE_LIBPNG
	{ IMG2_FORMAT_PNG,  "PNG (libpng)",   ".png",  img2_encode_png  },
#endif

#ifdef IMG2_HAVE_HEIF
	{ IMG2_FORMAT_HEIF, "HEIF (libheif)", ".heif", img2_encode_heif },
#endif

#ifdef IMG2_HAVE_WEBP
	{ IMG2_FORMAT_WEBP, "WebP (libwebp)", ".webp", img2_encode_webp },
#endif

#ifdef IMG2_HAVE_JXL
	{ IMG2_FORMAT_JXL,  "JXL (libjxl)",   ".jxl",  img2_encode_jxl  },
#endif
};

/**
 * @brief Global registry pointer and count
 */
const img2_encoder_entry_t *img2_encoder_registry = NULL;
size_t img2_encoder_registry_size = 0;

/**
 * @brief Initialize encoder registry
 *
 * Sets up the global registry pointers. This function is idempotent
 * and safe to call multiple times.
 */
void img2_encoder_registry_init(void)
{
	if (img2_encoder_registry != NULL) {
		/* Already initialized */
		return;
	}

	img2_encoder_registry = s_encoder_registry;
	img2_encoder_registry_size = sizeof(s_encoder_registry) / sizeof(img2_encoder_entry_t);

	img2_log(IMG2_LOG_INFO, "Encoder registry initialized with %zu encoders:", img2_encoder_registry_size);
	for (size_t i = 0; i < img2_encoder_registry_size; i++) {
		img2_log(IMG2_LOG_INFO, "  [%zu] %s (%s)", i, s_encoder_registry[i].name, s_encoder_registry[i].extension);
	}
}

/**
 * @brief Find encoder by output format
 *
 * Performs linear search through the registry to find an encoder
 * that handles the specified output format.
 */
const img2_encoder_entry_t *img2_encoder_find(img2_format_t format)
{
	if (img2_encoder_registry == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Encoder registry not initialized (call encoder_registry_init())");
		return NULL;
	}

	/* Linear search through registry */
	for (size_t i = 0; i < img2_encoder_registry_size; i++) {
		if (img2_encoder_registry[i].format == format) {
			return &img2_encoder_registry[i];
		}
	}

	/* No encoder found */
	const char *format_name = (format == IMG2_FORMAT_JPEG) ? "JPEG" : (format == IMG2_FORMAT_PNG) ? "PNG" : (format == IMG2_FORMAT_HEIF) ? "HEIF" : (format == IMG2_FORMAT_WEBP) ? "WebP" : (format == IMG2_FORMAT_JXL) ? "JXL" : "UNKNOWN";
	img2_log(IMG2_LOG_ERROR, "Error: No encoder found for format: %s", format_name);
	return NULL;
}

/**
 * @brief Encode image data with specified format
 *
 * Main encoding dispatcher with comprehensive error checking and validation.
 */
int img2_encoder_dispatch(const img2_image_t *img, img2_format_t format, int quality, uint8_t **out_data, size_t *out_size)
{
	/* Validate inputs */
	if (img == NULL || out_data == NULL || out_size == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to encoder_encode");
		return -1;
	}

	/* Initialize outputs */
	*out_data = NULL;
	*out_size = 0;

	/* Validate format */
	if (format == IMG2_FORMAT_NONE) {
		img2_log(IMG2_LOG_ERROR, "Error: No output format specified");
		return -1;
	}

	/* Find appropriate encoder */
	const img2_encoder_entry_t *encoder = img2_encoder_find(format);
	if (encoder == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: No encoder available for format");
		return -1;
	}

	/* Call encoder function */
	int result = encoder->encode(img, quality, out_data, out_size);
	if (result != 0) {
		img2_log(IMG2_LOG_ERROR, "Error: Encoder '%s' failed to encode image", encoder->name);
		return -1;
	}

	/* Validate output */
	if (*out_data == NULL || *out_size == 0) {
		img2_log(IMG2_LOG_ERROR, "Error: Encoder returned invalid output (data=%p, size=%zu)", (void *)*out_data, *out_size);
		if (*out_data != NULL) {
			free(*out_data);
			*out_data = NULL;
		}
		*out_size = 0;
		return -1;
	}

	return 0;
}
