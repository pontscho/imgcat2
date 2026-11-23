/**
 * @file encoder_webp.c
 * @brief WebP encoder implementation using libwebp
 *
 * Encodes RGBA8888 images to WebP format using libwebp.
 * Supports both lossy and lossless compression modes.
 */

/* clang-format off */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webp/encode.h>
/* clang-format on */

#include "encoder.h"
#include "../core/log.h"

/**
 * @brief Encode RGBA image to WebP format
 *
 * Encodes RGBA8888 image to WebP format with quality control.
 * Supports both lossy and lossless modes:
 * - quality 0-99: lossy compression
 * - quality 100: lossless compression
 *
 * @param img Input image in RGBA8888 format
 * @param quality WebP quality level (0-100, higher = better quality, larger file)
 *                Quality 100 triggers lossless mode
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return 0 on success, -1 on error
 *
 * @note WebP preserves alpha channel (supports transparency)
 * @note Caller must free *out_data with free() when done
 */
img2_status_t img2_encode_webp(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size)
{
	return img2_encode_webp_opts(img, quality, NULL, out_data, out_size);
}

/** Quality factor of libwebp's WebPEncodeLosslessRGBA() (LOSSLESS_DEFAULT_QUALITY in picture_enc.c) */
#define WEBP_LOSSLESS_QUALITY 70.0f

/**
 * @brief img2_encode_webp() with encode options (NULL = defaults)
 *
 * WebPConfig + WebPEncode, set up exactly like libwebp's simple API (WebPEncodeRGBA / WebPEncodeLosslessRGBA:
 * WEBP_PRESET_DEFAULT, method 4, alpha_quality 100, lossless with quality factor 70), so the default options
 * give the same bytes. lossless (or quality 100): lossless mode; effort e: method round((e-1)*6/9);
 * alpha_quality; threads > 1: thread_level 1. chroma other than 4:2:0 and progressive: one WARN each.
 */
img2_status_t img2_encode_webp_opts(const img2_image_t *img, int quality, const img2_encode_opts_t *opts, uint8_t **out_data, size_t *out_size)
{
	if (img == NULL || out_data == NULL || out_size == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to encode_webp");
		return IMG2_E_ARG;
	}

	/* Initialize outputs */
	*out_data = NULL;
	*out_size = 0;

	/* Clamp quality to valid range [0, 100] */
	int webp_quality = quality;
	if (webp_quality < 0) {
		webp_quality = 0;
	}
	if (webp_quality > 100) {
		webp_quality = 100;
	}

	const img2_encode_opts_t *o = img2_encode_opts_or_default(opts);
	const bool lossless = o->lossless || webp_quality == 100;
	if (o->chroma == IMG2_CHROMA_422 || o->chroma == IMG2_CHROMA_444) {
		char option[16];
		snprintf(option, sizeof(option), "chroma %s", img2_chroma_name(o->chroma));
		img2_encode_warn_ignored(option, "WebP", "always 4:2:0");
	}
	if (o->progressive) {
		img2_encode_warn_ignored("progressive", "WebP", "no progressive mode");
	}
	if (lossless && o->alpha_quality > 0) {
		img2_encode_warn_ignored("alpha_quality", "lossless WebP", "alpha is lossless");
	}

	WebPConfig config;
	WebPPicture pic;
	if (!WebPConfigPreset(&config, WEBP_PRESET_DEFAULT, lossless ? WEBP_LOSSLESS_QUALITY : (float)webp_quality) || !WebPPictureInit(&pic)) {
		img2_log(IMG2_LOG_ERROR, "Error: libwebp version mismatch");
		return IMG2_E_CODEC;
	}
	config.lossless = lossless ? 1 : 0;
	if (o->effort > 0) {
		config.method = ((o->effort - 1) * 12 + 9) / 18; /* round((e-1)*6/9): 1 -> 0 ... 7 -> 4 ... 10 -> 6 */
	}
	if (o->alpha_quality > 0 && !lossless) {
		config.alpha_quality = o->alpha_quality;
	}
	if (o->threads > 1) {
		config.thread_level = 1;
	}

	/* Encode to WebP */
	WebPMemoryWriter writer;
	WebPMemoryWriterInit(&writer);
	pic.use_argb = lossless ? 1 : 0;
	pic.width = (int)img->width;
	pic.height = (int)img->height;
	pic.writer = WebPMemoryWrite;
	pic.custom_ptr = &writer;
	int ok = WebPPictureImportRGBA(&pic, img->pixels, (int)img->width * 4) && WebPEncode(&config, &pic);
	WebPPictureFree(&pic);
	uint8_t *webp_buffer = writer.mem;
	size_t webp_size = writer.size;

	if (!ok) { /* WebPEncode() succeeds only after writing the RIFF header: ok implies a non-empty buffer */
		img2_log(IMG2_LOG_ERROR, "Error: Failed to encode WebP image");
		WebPMemoryWriterClear(&writer);
		return IMG2_E_CODEC;
	}

	/* Allocate and copy output data */
	*out_data = (uint8_t *)malloc(webp_size);
	if (*out_data == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate output buffer (%zu bytes)", webp_size);
		WebPFree(webp_buffer);
		return IMG2_E_NOMEM;
	}

	memcpy(*out_data, webp_buffer, webp_size);
	*out_size = webp_size;

	/* Free WebP internal buffer */
	WebPFree(webp_buffer);

	return IMG2_OK;
}
