/**
 * @file encoder.c
 * @brief Image encoder registry and dispatch implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "encoder.h"

/**
 * @brief Forward declarations for encoder functions
 *
 * These will be implemented in separate encoder implementation files
 * (encoder_jpeg.c, encoder_png.c). For now, we declare them
 * to satisfy the registry structure.
 */

#ifdef HAVE_LIBJPEG
extern int encode_jpeg(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef HAVE_LIBPNG
extern int encode_png(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef HAVE_WEBP
extern int encode_webp(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef HAVE_HEIF
extern int encode_heif(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

#ifdef HAVE_JXL
extern int encode_jxl(const image_t *img, int quality, uint8_t **out_data, size_t *out_size);
#endif

/**
 * @brief Static encoder registry array
 *
 * Populated at compile-time based on HAVE_* preprocessor flags.
 * Conditional compilation ensures only available encoders are registered.
 */
static const encoder_t s_encoder_registry[] = {
#ifdef HAVE_LIBJPEG
	{ FORMAT_JPEG, "JPEG (libjpeg)", ".jpg",  encode_jpeg },
#endif

#ifdef HAVE_LIBPNG
	{ FORMAT_PNG,  "PNG (libpng)",   ".png",  encode_png  },
#endif

#ifdef HAVE_HEIF
	{ FORMAT_HEIF, "HEIF (libheif)", ".heif", encode_heif },
#endif

#ifdef HAVE_WEBP
	{ FORMAT_WEBP, "WebP (libwebp)", ".webp", encode_webp },
#endif

#ifdef HAVE_JXL
	{ FORMAT_JXL,  "JXL (libjxl)",   ".jxl",  encode_jxl  },
#endif
};

/**
 * @brief Global registry pointer and count
 */
const encoder_t *g_encoder_registry = NULL;
size_t g_encoder_count = 0;

/**
 * @brief Initialize encoder registry
 *
 * Sets up the global registry pointers. This function is idempotent
 * and safe to call multiple times.
 */
void encoder_registry_init(const cli_options_t *opts)
{
	if (g_encoder_registry != NULL) {
		/* Already initialized */
		return;
	}

	g_encoder_registry = s_encoder_registry;
	g_encoder_count = sizeof(s_encoder_registry) / sizeof(encoder_t);

	if (opts != NULL && !opts->silent) {
		fprintf(stderr, "Encoder registry initialized with %zu encoders:\n", g_encoder_count);
		for (size_t i = 0; i < g_encoder_count; i++) {
			fprintf(stderr, "  [%zu] %s (%s)\n", i, s_encoder_registry[i].name, s_encoder_registry[i].extension);
		}
	}
}

/**
 * @brief Find encoder by output format
 *
 * Performs linear search through the registry to find an encoder
 * that handles the specified output format.
 */
const encoder_t *encoder_find_by_format(output_format_t format)
{
	if (g_encoder_registry == NULL) {
		fprintf(stderr, "Error: Encoder registry not initialized (call encoder_registry_init())\n");
		return NULL;
	}

	/* Linear search through registry */
	for (size_t i = 0; i < g_encoder_count; i++) {
		if (g_encoder_registry[i].format == format) {
			return &g_encoder_registry[i];
		}
	}

	/* No encoder found */
	const char *format_name = (format == FORMAT_JPEG) ? "JPEG" : (format == FORMAT_PNG) ? "PNG" : (format == FORMAT_HEIF) ? "HEIF" : (format == FORMAT_WEBP) ? "WebP" : (format == FORMAT_JXL) ? "JXL" : "UNKNOWN";
	fprintf(stderr, "Error: No encoder found for format: %s\n", format_name);
	return NULL;
}

/**
 * @brief Encode image data with specified format
 *
 * Main encoding dispatcher with comprehensive error checking and validation.
 */
int encoder_encode(const image_t *img, output_format_t format, int quality, uint8_t **out_data, size_t *out_size)
{
	/* Validate inputs */
	if (img == NULL || out_data == NULL || out_size == NULL) {
		fprintf(stderr, "Error: Invalid parameters to encoder_encode\n");
		return -1;
	}

	/* Initialize outputs */
	*out_data = NULL;
	*out_size = 0;

	/* Validate format */
	if (format == FORMAT_NONE) {
		fprintf(stderr, "Error: No output format specified\n");
		return -1;
	}

	/* Find appropriate encoder */
	const encoder_t *encoder = encoder_find_by_format(format);
	if (encoder == NULL) {
		fprintf(stderr, "Error: No encoder available for format\n");
		return -1;
	}

	/* Call encoder function */
	int result = encoder->encode(img, quality, out_data, out_size);
	if (result != 0) {
		fprintf(stderr, "Error: Encoder '%s' failed to encode image\n", encoder->name);
		return -1;
	}

	/* Validate output */
	if (*out_data == NULL || *out_size == 0) {
		fprintf(stderr, "Error: Encoder returned invalid output (data=%p, size=%zu)\n", (void *)*out_data, *out_size);
		if (*out_data != NULL) {
			free(*out_data);
			*out_data = NULL;
		}
		*out_size = 0;
		return -1;
	}

	return 0;
}
