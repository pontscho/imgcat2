/**
 * @file decoder.c
 * @brief Image decoder registry and dispatch implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decoder.h"
#include "../core/log.h"

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

#ifdef HAVE_WEBP
extern image_t **decode_webp(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef HAVE_HEIF
extern image_t **decode_heif(const uint8_t *data, size_t len, int *frame_count);
extern image_t **decode_avif(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef HAVE_TIFF
extern image_t **decode_tiff(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef HAVE_RAW
extern image_t **decode_raw(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef HAVE_JXL
extern image_t **decode_jxl(const uint8_t *data, size_t len, int *frame_count);
#endif

/* SVG decoders */
extern image_t **decode_svg_nanosvg(const uint8_t *data, size_t len, int *frame_count);
#ifdef HAVE_RESVG
extern image_t **decode_svg_resvg(const uint8_t *data, size_t len, int *frame_count);
#endif

/* QOI decoder */
extern image_t **decode_qoi(const uint8_t *data, size_t len, int *frame_count);

/* ICO/CUR decoder */
extern image_t **decode_ico(const uint8_t *data, size_t len, int *frame_count);

/* Netpbm (PBM/PGM/PPM/PAM) decoder */
extern image_t **decode_netpbm(const uint8_t *data, size_t len, int *frame_count);

/* STB fallback decoders */
extern image_t **decode_stb(const uint8_t *data, size_t len, int *frame_count);

/**
 * @brief SVG decoder dispatcher
 *
 * Tries resvg first if available, falls back to nanosvg on error.
 * This provides automatic fallback for better SVG compatibility.
 *
 * @param data Raw SVG file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 */
static image_t **decode_svg(const uint8_t *data, size_t len, int *frame_count)
{
#ifdef HAVE_RESVG
	image_t **result = decode_svg_resvg(data, len, frame_count);
	if (result != NULL) {
		return result;
	}
#endif

	// Use nanosvg (always available)
	return decode_svg_nanosvg(data, len, frame_count);
}

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

#ifdef HAVE_WEBP
	{ MIME_WEBP, "WebP (libwebp)",       decode_webp         },
#endif

#ifdef HAVE_HEIF
	{ MIME_HEIF, "HEIF (libheif)",       decode_heif         },
	{ MIME_AVIF, "AVIF (libheif)",       decode_avif         },
#endif

#ifdef HAVE_TIFF
	{ MIME_TIFF, "TIFF (libtiff)",       decode_tiff         },
#endif

#ifdef HAVE_RAW
	{ MIME_RAW,  "RAW (libraw)",         decode_raw          },
#endif

#ifdef HAVE_JXL
	{ MIME_JXL,  "JXL (libjxl)",         decode_jxl          },
#endif

/* SVG format */
#ifdef HAVE_RESVG
	{ MIME_SVG,  "SVG (resvg)",          decode_svg          },
#else
	{ MIME_SVG, "SVG (nanosvg)",         decode_svg          },
#endif

	/* QOI format */
	{ MIME_QOI,  "QOI (header-only)",    decode_qoi          },

	/* ICO/CUR formats */
	{ MIME_ICO,  "ICO (custom)",         decode_ico          },
	{ MIME_CUR,  "CUR (custom)",         decode_ico          },

	/* Netpbm formats (P1-P7) */
	{ MIME_PNM,  "PNM/PAM (native)",     decode_netpbm       },

	/* STB-supported formats */
	{ MIME_BMP,  "BMP (stb_image)",      decode_stb          },
	{ MIME_TGA,  "TGA (stb_image)",      decode_stb          },
	{ MIME_PSD,  "PSD (stb_image)",      decode_stb          },
	{ MIME_HDR,  "HDR (stb_image)",      decode_stb          },
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
void decoder_registry_init(void)
{
	if (g_decoder_registry != NULL) {
		// Already initialized
		return;
	}

	g_decoder_registry = s_decoder_registry;
	g_decoder_count = sizeof(s_decoder_registry) / sizeof(decoder_t);

	img2_log(IMG2_LOG_INFO, "Decoder registry initialized with %zu decoders:", g_decoder_count);
	for (size_t i = 0; i < g_decoder_count; i++) {
		img2_log(IMG2_LOG_INFO, "  [%zu] %s", i, s_decoder_registry[i].name);
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
		img2_log(IMG2_LOG_ERROR, "Error: Decoder registry not initialized (call decoder_registry_init())");
		return NULL;
	}

	// Linear search through registry
	for (size_t i = 0; i < g_decoder_count; i++) {
		if (g_decoder_registry[i].mime_type == mime) {
			return &g_decoder_registry[i];
		}
	}

	// No decoder found
	img2_log(IMG2_LOG_ERROR, "Error: No decoder found for MIME type: %s", mime_type_name(mime));
	return NULL;
}

/**
 * @brief Decode image data with automatic format detection
 *
 * Main decoding dispatcher with comprehensive error checking and validation.
 */
image_t **decoder_decode(const img2_decode_opts_t *opts, const uint8_t *data, size_t len, mime_type_t mime, int *frame_count)
{
	// Validate inputs
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decoder_decode");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	image_t **frames = NULL;

	if (mime == MIME_YUV) {
		// Raw YUV is headerless: it is not in the registry and needs the --yuv geometry
		if (opts == NULL || opts->yuv == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Raw YUV input requires --yuv format:width:height");
			return NULL;
		}

		img2_log(IMG2_LOG_INFO, "Decoding %zu bytes with decoder: YUV raw (%s %ux%u)", len, yuv_format_name(opts->yuv->format), opts->yuv->width, opts->yuv->height);

		frames = decode_yuv(data, len, opts->yuv, frame_count);
		if (frames == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Decoder 'YUV raw' failed to decode image");
			return NULL;
		}

	} else {
		// Find appropriate decoder
		const decoder_t *decoder = decoder_find_by_mime(mime);
		if (decoder == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: No decoder available for format: %s", mime_type_name(mime));
			return NULL;
		}

		img2_log(IMG2_LOG_INFO, "Decoding %zu bytes with decoder: %s", len, decoder->name);

		// Call decoder function
		frames = decoder->decode(data, len, frame_count);
		if (frames == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Decoder '%s' failed to decode image", decoder->name);
			return NULL;
		}
	}

	// Validate output
	if (*frame_count <= 0) {
		img2_log(IMG2_LOG_ERROR, "Error: Decoder returned invalid frame count: %d", *frame_count);
		decoder_free_frames(frames, *frame_count);
		return NULL;
	}

	// Validate each frame
	for (int i = 0; i < *frame_count; i++) {
		if (frames[i] == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Decoder returned NULL frame at index %d", i);
			decoder_free_frames(frames, *frame_count);
			return NULL;
		}

		// Check dimensions within limits
		if (frames[i]->width == 0 || frames[i]->height == 0) {
			img2_log(IMG2_LOG_ERROR, "Error: Frame %d has invalid dimensions: %ux%u", i, frames[i]->width, frames[i]->height);
			decoder_free_frames(frames, *frame_count);
			return NULL;
		}

		if (frames[i]->width > IMAGE_MAX_DIMENSION || frames[i]->height > IMAGE_MAX_DIMENSION) {
			img2_log(IMG2_LOG_ERROR, "Error: Frame %d dimensions exceed maximum (%u): %ux%u", i, IMAGE_MAX_DIMENSION, frames[i]->width, frames[i]->height);
			decoder_free_frames(frames, *frame_count);
			return NULL;
		}

		// Check pixel buffer allocated
		if (frames[i]->pixels == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Frame %d has NULL pixel buffer", i);
			decoder_free_frames(frames, *frame_count);
			return NULL;
		}
	}

	img2_log(IMG2_LOG_INFO, "Successfully decoded %d frame(s) with dimensions: %ux%u", *frame_count, frames[0]->width, frames[0]->height);

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
