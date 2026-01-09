/**
 * @file decoder.c
 * @brief Image decoder registry and dispatch implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decoder.h"

/**
 * @brief Forward declarations for decoder functions
 *
 * These will be implemented in separate decoder implementation files
 * (decoder_png.c, decoder_jpeg.c, etc.). For now, we declare them
 * to satisfy the registry structure.
 */

#ifdef HAVE_LIBPNG
extern image_t **decode_png(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef HAVE_LIBJPEG
extern image_t **decode_jpeg(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef HAVE_GIFLIB
extern image_t **decode_gif(const uint8_t *data, size_t len, int *frame_count);
extern image_t **decode_gif_animated(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef ENABLE_WEBP
extern image_t **decode_webp(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef ENABLE_HEIF
extern image_t **decode_heif(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef ENABLE_TIFF
extern image_t **decode_tiff(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef ENABLE_RAW
extern image_t **decode_raw(const uint8_t *data, size_t len, int *frame_count);
#endif

/* QOI decoder (header-only, always available) */
extern image_t **decode_qoi(const uint8_t *data, size_t len, int *frame_count);

/* ICO/CUR decoder (always available) */
extern image_t **decode_ico(const uint8_t *data, size_t len, int *frame_count);

/* STB fallback decoders (always available) */
extern image_t **decode_stb(const uint8_t *data, size_t len, int *frame_count);

/**
 * @brief Static decoder registry array
 *
 * Populated at compile-time based on HAVE_* preprocessor flags.
 * Priority: native libraries > STB fallbacks.
 */
static const decoder_t s_decoder_registry[] = {
#ifdef HAVE_LIBPNG
	{ MIME_PNG,  "PNG (libpng)",         decode_png          },
#else
	{ MIME_PNG, "PNG (stb_image)", decode_stb },
#endif

#ifdef HAVE_LIBJPEG
	{ MIME_JPEG, "JPEG (libjpeg-turbo)", decode_jpeg         },
#else
	{ MIME_JPEG, "JPEG (stb_image)", decode_stb },
#endif

#ifdef HAVE_GIFLIB
	{ MIME_GIF,  "GIF (giflib)",         decode_gif_animated },
#endif

#ifdef ENABLE_WEBP
	{ MIME_WEBP, "WebP (libwebp)",       decode_webp         },
#endif

#ifdef ENABLE_HEIF
	{ MIME_HEIF, "HEIF (libheif)",       decode_heif         },
#endif

#ifdef ENABLE_TIFF
	{ MIME_TIFF, "TIFF (libtiff)",       decode_tiff         },
#endif

#ifdef ENABLE_RAW
	{ MIME_RAW,  "RAW (libraw)",         decode_raw          },
#endif

	/* QOI format (header-only, always available) */
	{ MIME_QOI,  "QOI (header-only)",    decode_qoi          },

	/* ICO/CUR formats (custom decoder, always available) */
	{ MIME_ICO,  "ICO (custom)",         decode_ico          },
	{ MIME_CUR,  "CUR (custom)",         decode_ico          },

	/* STB-supported formats (always available) */
	{ MIME_BMP,  "BMP (stb_image)",      decode_stb          },
	{ MIME_TGA,  "TGA (stb_image)",      decode_stb          },
	{ MIME_PSD,  "PSD (stb_image)",      decode_stb          },
	{ MIME_HDR,  "HDR (stb_image)",      decode_stb          },
	{ MIME_PNM,  "PNM (stb_image)",      decode_stb          },
};

/**
 * @brief Global registry pointer and count
 */
const decoder_t *g_decoder_registry = NULL;
size_t g_decoder_count = 0;

/**
 * @brief Initialize decoder registry
 *
 * Sets up the global registry pointers. This function is idempotent
 * and safe to call multiple times.
 */
void decoder_registry_init(cli_options_t *opts)
{
	if (g_decoder_registry != NULL) {
		// Already initialized
		return;
	}

	g_decoder_registry = s_decoder_registry;
	g_decoder_count = sizeof(s_decoder_registry) / sizeof(decoder_t);

	if (opts != NULL && !opts->silent) {
		fprintf(stderr, "Decoder registry initialized with %zu decoders:\n", g_decoder_count);
		for (size_t i = 0; i < g_decoder_count; i++) {
			fprintf(stderr, "  [%zu] %s\n", i, s_decoder_registry[i].name);
		}
	}
}

/**
 * @brief Find decoder by MIME type
 *
 * Performs linear search through the registry to find a decoder
 * that handles the specified MIME type.
 */
const decoder_t *decoder_find_by_mime(mime_type_t mime)
{
	if (g_decoder_registry == NULL) {
		fprintf(stderr, "Error: Decoder registry not initialized (call decoder_registry_init())\n");
		return NULL;
	}

	// Linear search through registry
	for (size_t i = 0; i < g_decoder_count; i++) {
		if (g_decoder_registry[i].mime_type == mime) {
			return &g_decoder_registry[i];
		}
	}

	// No decoder found
	fprintf(stderr, "Error: No decoder found for MIME type: %s\n", mime_type_name(mime));
	return NULL;
}

/**
 * @brief Decode image data with automatic format detection
 *
 * Main decoding dispatcher with comprehensive error checking and validation.
 */
image_t **decoder_decode(cli_options_t *opts, const uint8_t *data, size_t len, mime_type_t mime, int *frame_count)
{
	// Validate inputs
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decoder_decode\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Find appropriate decoder
	const decoder_t *decoder = decoder_find_by_mime(mime);
	if (decoder == NULL) {
		fprintf(stderr, "Error: No decoder available for format: %s\n", mime_type_name(mime));
		return NULL;
	}

	if (opts != NULL && !opts->silent) {
		fprintf(stderr, "Decoding %zu bytes with decoder: %s\n", len, decoder->name);
	}

	// Call decoder function
	image_t **frames = decoder->decode(data, len, frame_count);
	if (frames == NULL) {
		fprintf(stderr, "Error: Decoder '%s' failed to decode image\n", decoder->name);
		return NULL;
	}

	// Validate output
	if (*frame_count <= 0) {
		fprintf(stderr, "Error: Decoder returned invalid frame count: %d\n", *frame_count);
		decoder_free_frames(frames, *frame_count);
		return NULL;
	}

	// Validate each frame
	for (int i = 0; i < *frame_count; i++) {
		if (frames[i] == NULL) {
			fprintf(stderr, "Error: Decoder returned NULL frame at index %d\n", i);
			decoder_free_frames(frames, *frame_count);
			return NULL;
		}

		// Check dimensions within limits
		if (frames[i]->width == 0 || frames[i]->height == 0) {
			fprintf(stderr, "Error: Frame %d has invalid dimensions: %ux%u\n", i, frames[i]->width, frames[i]->height);
			decoder_free_frames(frames, *frame_count);
			return NULL;
		}

		if (frames[i]->width > IMAGE_MAX_DIMENSION || frames[i]->height > IMAGE_MAX_DIMENSION) {
			fprintf(stderr, "Error: Frame %d dimensions exceed maximum (%u): %ux%u\n", i, IMAGE_MAX_DIMENSION, frames[i]->width, frames[i]->height);
			decoder_free_frames(frames, *frame_count);
			return NULL;
		}

		// Check pixel buffer allocated
		if (frames[i]->pixels == NULL) {
			fprintf(stderr, "Error: Frame %d has NULL pixel buffer\n", i);
			decoder_free_frames(frames, *frame_count);
			return NULL;
		}
	}

	if (opts != NULL && !opts->silent) {
		fprintf(stderr, "Successfully decoded %d frame(s) with dimensions: %ux%u\n", *frame_count, frames[0]->width, frames[0]->height);
	}

	return frames;
}

/**
 * @brief Free multi-frame decoder output
 *
 * Cleans up all memory allocated by decoder functions.
 * NULL-safe and handles edge cases gracefully.
 */
void decoder_free_frames(image_t **frames, int frame_count)
{
	if (frames == NULL) {
		return;
	}

	// Free each frame
	for (int i = 0; i < frame_count; i++) {
		if (frames[i] != NULL) {
			image_destroy(frames[i]);
		}
	}

	// Free frames array itself
	free(frames);
}
