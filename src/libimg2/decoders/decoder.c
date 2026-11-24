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
 * @brief SVG decoder dispatcher
 *
 * Tries resvg first if available, falls back to nanosvg on error.
 * This provides automatic fallback for better SVG compatibility.
 */
static img2_status_t decode_svg(const uint8_t *data, size_t len, const img2_decode_opts_t *opts, img2_frames_t *out)
{
#ifdef IMG2_HAVE_RESVG
	if (img2_decode_svg_resvg(data, len, opts, out) == IMG2_OK) {
		return IMG2_OK;
	}
#endif

	// Use nanosvg (always available)
	return img2_decode_svg_nanosvg(data, len, opts, out);
}

#ifdef PNG_APNG_SUPPORTED
#define IMG2_PNG_PROBE img2_png_is_animated
#else
#define IMG2_PNG_PROBE NULL
#endif

/**
 * @brief Static decoder table
 *
 * Populated at compile-time based on IMG2_HAVE_* preprocessor flags.
 * Priority: native libraries > STB fallbacks.
 */
static const img2_decoder_entry_t s_decoders[] = {
#ifdef IMG2_HAVE_LIBPNG
	{ { IMG2_MIME_PNG,  "PNG (libpng)"         }, img2_decode_png,          IMG2_PNG_PROBE        },
#else
	{ { IMG2_MIME_PNG,  "PNG (stb_image)"      }, img2_decode_stb,          NULL                  },
#endif

#ifdef IMG2_HAVE_LIBJPEG
	{ { IMG2_MIME_JPEG, "JPEG (libjpeg-turbo)" }, img2_decode_jpeg,         NULL                  },
#else
	{ { IMG2_MIME_JPEG, "JPEG (stb_image)"     }, img2_decode_stb,          NULL                  },
#endif

#ifdef IMG2_HAVE_GIFLIB
	{ { IMG2_MIME_GIF,  "GIF (giflib)"         }, img2_decode_gif_animated, img2_gif_is_animated  },
#endif

#ifdef IMG2_HAVE_WEBP
	{ { IMG2_MIME_WEBP, "WebP (libwebp)"       }, img2_decode_webp,         img2_webp_is_animated },
#endif

#ifdef IMG2_HAVE_HEIF
	{ { IMG2_MIME_HEIF, "HEIF (libheif)"       }, img2_decode_heif,         img2_heif_is_animated },
	{ { IMG2_MIME_AVIF, "AVIF (libheif)"       }, img2_decode_avif,         img2_heif_is_animated },
#endif

#ifdef IMG2_HAVE_TIFF
	{ { IMG2_MIME_TIFF, "TIFF (libtiff)"       }, img2_decode_tiff,         NULL                  },
#endif

#ifdef IMG2_HAVE_RAW
	{ { IMG2_MIME_RAW,  "RAW (libraw)"         }, img2_decode_raw,          NULL                  },
#endif

#ifdef IMG2_HAVE_JXL
	{ { IMG2_MIME_JXL,  "JXL (libjxl)"         }, img2_decode_jxl,          NULL                  },
#endif

/* SVG format */
#ifdef IMG2_HAVE_RESVG
	{ { IMG2_MIME_SVG,  "SVG (resvg)"          }, decode_svg,               NULL                  },
#else
	{ { IMG2_MIME_SVG,  "SVG (nanosvg)"        }, decode_svg,               NULL                  },
#endif

	/* QOI format */
	{ { IMG2_MIME_QOI,  "QOI (header-only)"    }, img2_decode_qoi,          NULL                  },

	/* ICO/CUR formats */
	{ { IMG2_MIME_ICO,  "ICO (custom)"         }, img2_decode_ico,          NULL                  },
	{ { IMG2_MIME_CUR,  "CUR (custom)"         }, img2_decode_ico,          NULL                  },

	/* Netpbm formats (P1-P7) */
	{ { IMG2_MIME_PNM,  "PNM/PAM (native)"     }, img2_decode_pnm,          NULL                  },

	/* STB-supported formats */
	{ { IMG2_MIME_BMP,  "BMP (stb_image)"      }, img2_decode_stb,          NULL                  },
	{ { IMG2_MIME_TGA,  "TGA (stb_image)"      }, img2_decode_stb,          NULL                  },
	{ { IMG2_MIME_PSD,  "PSD (stb_image)"      }, img2_decode_stb,          NULL                  },
	{ { IMG2_MIME_HDR,  "HDR (stb_image)"      }, img2_decode_stb,          NULL                  },
};

#define DECODER_COUNT (sizeof(s_decoders) / sizeof(s_decoders[0]))

size_t img2_decoder_count(void)
{
	return DECODER_COUNT;
}

const img2_decoder_info_t *img2_decoder_info(size_t index)
{
	return index < DECODER_COUNT ? &s_decoders[index].info : NULL;
}

/**
 * @brief Log the decoder table (the table is const; nothing to initialise)
 */
void img2_decoder_registry_init(void)
{
	img2_log(IMG2_LOG_INFO, "Decoder registry initialized with %zu decoders:", DECODER_COUNT);
	for (size_t i = 0; i < DECODER_COUNT; i++) {
		img2_log(IMG2_LOG_INFO, "  [%zu] %s", i, s_decoders[i].info.name);
	}
}

/**
 * @brief Find decoder by MIME type ([R2] silent)
 */
const img2_decoder_entry_t *img2_decoder_find(img2_mime_t mime)
{
	for (size_t i = 0; i < DECODER_COUNT; i++) {
		if (s_decoders[i].info.mime == mime) {
			return &s_decoders[i];
		}
	}
	return NULL;
}

bool img2_can_decode(img2_mime_t mime)
{
	return mime == IMG2_MIME_YUV || img2_decoder_find(mime) != NULL;
}

bool img2_is_animated(const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0 || len > IMG2_MAX_FILE_SIZE) {
		return false;
	}
	const img2_decoder_entry_t *dec = img2_decoder_find(img2_detect_mime(data, len));
	if (dec == NULL || dec->is_animated == NULL) {
		return false;
	}
	return dec->is_animated(data, len);
}

img2_status_t img2_probe(const uint8_t *data, size_t len, img2_probe_info_t *out)
{
	/* the input checks of img2_decode(), at DEBUG */
	if (out == NULL) {
		img2_log(IMG2_LOG_DEBUG, "img2_probe: out is NULL");
		return IMG2_E_ARG;
	}
	memset(out, 0, sizeof(*out));
	if (data == NULL || len == 0) {
		img2_log(IMG2_LOG_DEBUG, "img2_probe: empty input");
		return IMG2_E_ARG;
	}
	if (len > IMG2_MAX_FILE_SIZE) {
		img2_log(IMG2_LOG_DEBUG, "img2_probe: input of %zu bytes exceeds IMG2_MAX_FILE_SIZE", len);
		return IMG2_E_LIMIT;
	}

	img2_mime_t mime = img2_detect_mime(data, len);
#ifdef IMG2_HAVE_LIBJPEG
	if (mime == IMG2_MIME_JPEG) {
		return img2_probe_jpeg(data, len, out); /* out stays zeroed on error */
	}
#endif

	/* No header-only path for this type: the full decode (its limits and log lines) */
	img2_frames_t fr;
	img2_status_t st = img2_decoder_dispatch(NULL, data, len, mime, &fr);
	if (st != IMG2_OK) {
		return st;
	}
	out->mime = mime;
	out->width = fr.frames[0]->width;
	out->height = fr.frames[0]->height;
	out->orientation = fr.frames[0]->orientation;
	img2_frames_free(&fr);
	return IMG2_OK;
}

img2_status_t img2_frames_adopt(img2_frames_t *out, img2_image_t **frames, int count, img2_status_t fail)
{
	if (out == NULL) {
		img2_decoder_free_frames(frames, count);
		return IMG2_E_ARG;
	}
	out->frames = NULL;
	out->count = 0;
	out->loop_count = 0;
	if (frames == NULL) {
		return fail != IMG2_OK ? fail : IMG2_E_INTERNAL;
	}
	out->frames = frames;
	out->count = count;
	return IMG2_OK;
}

/* Validation failure: free everything, zero *out */
static img2_status_t dispatch_fail(img2_frames_t *out, img2_status_t st)
{
	img2_frames_free(out);
	return st;
}

/**
 * @brief Decode image data
 *
 * Main decoding dispatcher with comprehensive error checking and validation.
 */
img2_status_t img2_decoder_dispatch(const img2_decode_opts_t *opts, const uint8_t *data, size_t len, img2_mime_t mime, img2_frames_t *out)
{
	if (out != NULL) {
		out->frames = NULL;
		out->count = 0;
		out->loop_count = 0;
	}

	// Validate inputs
	if (data == NULL || len == 0 || out == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to decoder_decode");
		return IMG2_E_ARG;
	}

	img2_status_t st;

	if (mime == IMG2_MIME_YUV) {
		// Raw YUV is headerless: it is not in the registry and needs the --yuv geometry
		if (opts == NULL || opts->yuv == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Raw YUV input requires --yuv format:width:height");
			return IMG2_E_ARG;
		}

		img2_log(IMG2_LOG_INFO, "Decoding %zu bytes with decoder: YUV raw (%s %ux%u)", len, img2_yuv_format_name(opts->yuv->format), opts->yuv->width, opts->yuv->height);

		st = img2_decode_yuv(data, len, opts, out);
		if (st != IMG2_OK) {
			img2_log(IMG2_LOG_ERROR, "Error: Decoder 'YUV raw' failed to decode image");
			return dispatch_fail(out, st);
		}

	} else {
		// Find appropriate decoder
		const img2_decoder_entry_t *decoder = img2_decoder_find(mime);
		if (decoder == NULL) {
			/* [R2] legacy lines of the former finder + dispatcher, same order */
			img2_log(IMG2_LOG_ERROR, "Error: No decoder found for MIME type: %s", img2_mime_name(mime));
			img2_log(IMG2_LOG_ERROR, "Error: No decoder available for format: %s", img2_mime_name(mime));
			return mime == IMG2_MIME_UNKNOWN ? IMG2_E_FORMAT : IMG2_E_UNSUPPORTED;
		}

		img2_log(IMG2_LOG_INFO, "Decoding %zu bytes with decoder: %s", len, decoder->info.name);

		// Call decoder function
		st = decoder->decode(data, len, opts, out);
		if (st != IMG2_OK) {
			img2_log(IMG2_LOG_ERROR, "Error: Decoder '%s' failed to decode image", decoder->info.name);
			return dispatch_fail(out, st);
		}
	}

	img2_image_t **frames = out->frames;

	// Validate output
	if (out->count <= 0 || frames == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Decoder returned invalid frame count: %d", out->count);
		return dispatch_fail(out, IMG2_E_FORMAT);
	}

	// Validate each frame
	for (int i = 0; i < out->count; i++) {
		if (frames[i] == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Decoder returned NULL frame at index %d", i);
			return dispatch_fail(out, IMG2_E_FORMAT);
		}

		// Check dimensions within limits
		if (frames[i]->width == 0 || frames[i]->height == 0) {
			img2_log(IMG2_LOG_ERROR, "Error: Frame %d has invalid dimensions: %ux%u", i, frames[i]->width, frames[i]->height);
			return dispatch_fail(out, IMG2_E_FORMAT);
		}

		if (frames[i]->width > IMG2_MAX_DIMENSION || frames[i]->height > IMG2_MAX_DIMENSION) {
			img2_log(IMG2_LOG_ERROR, "Error: Frame %d dimensions exceed maximum (%u): %ux%u", i, IMG2_MAX_DIMENSION, frames[i]->width, frames[i]->height);
			return dispatch_fail(out, IMG2_E_LIMIT);
		}

		// Check pixel buffer allocated
		if (frames[i]->pixels == NULL) {
			img2_log(IMG2_LOG_ERROR, "Error: Frame %d has NULL pixel buffer", i);
			return dispatch_fail(out, IMG2_E_FORMAT);
		}
	}

	img2_log(IMG2_LOG_INFO, "Successfully decoded %d frame(s) with dimensions: %ux%u", out->count, frames[0]->width, frames[0]->height);

	// Optional frame limit
	if (opts != NULL && opts->max_frames > 0 && out->count > opts->max_frames) {
		for (int i = opts->max_frames; i < out->count; i++) {
			img2_image_destroy(frames[i]);
			frames[i] = NULL;
		}
		out->count = opts->max_frames;
	}

	return IMG2_OK;
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
