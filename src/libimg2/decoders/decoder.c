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

#ifdef IMG2_HAVE_LIBPNG
extern img2_image_t **img2_decode_png(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef IMG2_HAVE_LIBJPEG
extern img2_image_t **img2_decode_jpeg(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef IMG2_HAVE_GIFLIB
extern img2_image_t **img2_decode_gif_animated(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef IMG2_HAVE_WEBP
extern img2_image_t **img2_decode_webp(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef IMG2_HAVE_HEIF
extern img2_image_t **img2_decode_heif(const uint8_t *data, size_t len, int *frame_count);
extern img2_image_t **img2_decode_avif(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef IMG2_HAVE_TIFF
extern img2_image_t **img2_decode_tiff(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef IMG2_HAVE_RAW
extern img2_image_t **img2_decode_raw(const uint8_t *data, size_t len, int *frame_count);
#endif

#ifdef IMG2_HAVE_JXL
extern img2_image_t **img2_decode_jxl(const uint8_t *data, size_t len, int *frame_count);
#endif

/* SVG decoders */
extern img2_image_t **img2_decode_svg_nanosvg(const uint8_t *data, size_t len, int *frame_count);
#ifdef IMG2_HAVE_RESVG
extern img2_image_t **img2_decode_svg_resvg(const uint8_t *data, size_t len, int *frame_count);
#endif

/* QOI decoder */
extern img2_image_t **img2_decode_qoi(const uint8_t *data, size_t len, int *frame_count);

/* ICO/CUR decoder */
extern img2_image_t **img2_decode_ico(const uint8_t *data, size_t len, int *frame_count);

/* Netpbm (PBM/PGM/PPM/PAM) decoder */
extern img2_image_t **img2_decode_pnm(const uint8_t *data, size_t len, int *frame_count);

/* STB fallback decoders */
extern img2_image_t **img2_decode_stb(const uint8_t *data, size_t len, int *frame_count);

/**
 * @brief SVG decoder dispatcher
 *
 * Tries resvg first if available, falls back to nanosvg on error.
 * This provides automatic fallback for better SVG compatibility.
 *
 * @param data Raw SVG file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single img2_image_t*, or NULL on error
 */
static img2_image_t **decode_svg(const uint8_t *data, size_t len, int *frame_count)
{
#ifdef IMG2_HAVE_RESVG
	img2_image_t **result = img2_decode_svg_resvg(data, len, frame_count);
	if (result != NULL) {
		return result;
	}
#endif

	// Use nanosvg (always available)
	return img2_decode_svg_nanosvg(data, len, frame_count);
}

/**
 * @brief Static decoder registry array
 *
 * Populated at compile-time based on HAVE_* preprocessor flags.
 * Priority: native libraries > STB fallbacks.
 */
static const img2_decoder_entry_t s_decoder_registry[] = {
#ifdef IMG2_HAVE_LIBPNG
	{ IMG2_MIME_PNG,  "PNG (libpng)",         img2_decode_png          },
#else
	{ IMG2_MIME_PNG, "PNG (stb_image)", img2_decode_stb },
#endif

#ifdef IMG2_HAVE_LIBJPEG
	{ IMG2_MIME_JPEG, "JPEG (libjpeg-turbo)", img2_decode_jpeg         },
#else
	{ IMG2_MIME_JPEG, "JPEG (stb_image)", img2_decode_stb },
#endif

#ifdef IMG2_HAVE_GIFLIB
	{ IMG2_MIME_GIF,  "GIF (giflib)",         img2_decode_gif_animated },
#endif

#ifdef IMG2_HAVE_WEBP
	{ IMG2_MIME_WEBP, "WebP (libwebp)",       img2_decode_webp         },
#endif

#ifdef IMG2_HAVE_HEIF
	{ IMG2_MIME_HEIF, "HEIF (libheif)",       img2_decode_heif         },
	{ IMG2_MIME_AVIF, "AVIF (libheif)",       img2_decode_avif         },
#endif

#ifdef IMG2_HAVE_TIFF
	{ IMG2_MIME_TIFF, "TIFF (libtiff)",       img2_decode_tiff         },
#endif

#ifdef IMG2_HAVE_RAW
	{ IMG2_MIME_RAW,  "RAW (libraw)",         img2_decode_raw          },
#endif

#ifdef IMG2_HAVE_JXL
	{ IMG2_MIME_JXL,  "JXL (libjxl)",         img2_decode_jxl          },
#endif

/* SVG format */
#ifdef IMG2_HAVE_RESVG
	{ IMG2_MIME_SVG,  "SVG (resvg)",          decode_svg          },
#else
	{ IMG2_MIME_SVG, "SVG (nanosvg)",         decode_svg          },
#endif

	/* QOI format */
	{ IMG2_MIME_QOI,  "QOI (header-only)",    img2_decode_qoi          },

	/* ICO/CUR formats */
	{ IMG2_MIME_ICO,  "ICO (custom)",         img2_decode_ico          },
	{ IMG2_MIME_CUR,  "CUR (custom)",         img2_decode_ico          },

	/* Netpbm formats (P1-P7) */
	{ IMG2_MIME_PNM,  "PNM/PAM (native)",     img2_decode_pnm       },

	/* STB-supported formats */
	{ IMG2_MIME_BMP,  "BMP (stb_image)",      img2_decode_stb          },
	{ IMG2_MIME_TGA,  "TGA (stb_image)",      img2_decode_stb          },
	{ IMG2_MIME_PSD,  "PSD (stb_image)",      img2_decode_stb          },
	{ IMG2_MIME_HDR,  "HDR (stb_image)",      img2_decode_stb          },
};

/**
 * @brief Global registry pointer and count
 */
const img2_decoder_entry_t *img2_decoder_registry = NULL;
size_t img2_decoder_registry_size = 0;

/**
 * @brief Initialize decoder registry
 *
 * Sets up the global registry pointers. This function is idempotent
 * and safe to call multiple times.
 */
void img2_decoder_registry_init(void)
{
	if (img2_decoder_registry != NULL) {
		// Already initialized
		return;
	}

	img2_decoder_registry = s_decoder_registry;
	img2_decoder_registry_size = sizeof(s_decoder_registry) / sizeof(img2_decoder_entry_t);

	img2_log(IMG2_LOG_INFO, "Decoder registry initialized with %zu decoders:", img2_decoder_registry_size);
	for (size_t i = 0; i < img2_decoder_registry_size; i++) {
		img2_log(IMG2_LOG_INFO, "  [%zu] %s", i, s_decoder_registry[i].name);
	}
}

/**
 * @brief Find decoder by MIME type
 *
 * Performs linear search through the registry to find a decoder
 * that handles the specified MIME type.
 */
const img2_decoder_entry_t *img2_decoder_find(img2_mime_t mime)
{
	if (img2_decoder_registry == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Decoder registry not initialized (call decoder_registry_init())");
		return NULL;
	}

	// Linear search through registry
	for (size_t i = 0; i < img2_decoder_registry_size; i++) {
		if (img2_decoder_registry[i].mime_type == mime) {
			return &img2_decoder_registry[i];
		}
	}

	// No decoder found
	img2_log(IMG2_LOG_ERROR, "Error: No decoder found for MIME type: %s", img2_mime_name(mime));
	return NULL;
}

/**
 * @brief Decode image data with automatic format detection
 *
 * Main decoding dispatcher with comprehensive error checking and validation.
 */
img2_image_t **img2_decoder_dispatch(const img2_decode_opts_t *opts, const uint8_t *data, size_t len, img2_mime_t mime, int *frame_count)
{
	// Validate inputs
	if (data == NULL || len == 0 || frame_count == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decoder_decode");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	img2_image_t **frames = NULL;

	if (mime == IMG2_MIME_YUV) {
		// Raw YUV is headerless: it is not in the registry and needs the --yuv geometry
		if (opts == NULL || opts->yuv == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Raw YUV input requires --yuv format:width:height");
			return NULL;
		}

		img2_log(IMG2_LOG_INFO, "Decoding %zu bytes with decoder: YUV raw (%s %ux%u)", len, img2_yuv_format_name(opts->yuv->format), opts->yuv->width, opts->yuv->height);

		frames = img2_decode_yuv(data, len, opts->yuv, frame_count);
		if (frames == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Decoder 'YUV raw' failed to decode image");
			return NULL;
		}

	} else {
		// Find appropriate decoder
		const img2_decoder_entry_t *decoder = img2_decoder_find(mime);
		if (decoder == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: No decoder available for format: %s", img2_mime_name(mime));
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
		img2_decoder_free_frames(frames, *frame_count);
		return NULL;
	}

	// Validate each frame
	for (int i = 0; i < *frame_count; i++) {
		if (frames[i] == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Decoder returned NULL frame at index %d", i);
			img2_decoder_free_frames(frames, *frame_count);
			return NULL;
		}

		// Check dimensions within limits
		if (frames[i]->width == 0 || frames[i]->height == 0) {
			img2_log(IMG2_LOG_ERROR, "Error: Frame %d has invalid dimensions: %ux%u", i, frames[i]->width, frames[i]->height);
			img2_decoder_free_frames(frames, *frame_count);
			return NULL;
		}

		if (frames[i]->width > IMG2_MAX_DIMENSION || frames[i]->height > IMG2_MAX_DIMENSION) {
			img2_log(IMG2_LOG_ERROR, "Error: Frame %d dimensions exceed maximum (%u): %ux%u", i, IMG2_MAX_DIMENSION, frames[i]->width, frames[i]->height);
			img2_decoder_free_frames(frames, *frame_count);
			return NULL;
		}

		// Check pixel buffer allocated
		if (frames[i]->pixels == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Frame %d has NULL pixel buffer", i);
			img2_decoder_free_frames(frames, *frame_count);
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
void img2_decoder_free_frames(img2_image_t **frames, int frame_count)
{
	if (frames == NULL) {
		return;
	}

	// Free each frame
	for (int i = 0; i < frame_count; i++) {
		if (frames[i] != NULL) {
			img2_image_destroy(frames[i]);
		}
	}

	// Free frames array itself
	free(frames);
}
