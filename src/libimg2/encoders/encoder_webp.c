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
#include <webp/mux.h>
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
 * @brief Add the EXIF / XMP chunks of @p meta to an encoded WebP through WebPMux
 *
 * @param assembled Output: the new file (WebPMalloc'ed, free with WebPFree / WebPDataClear)
 * @return IMG2_OK, or IMG2_E_NOMEM (WebPMux can only fail on memory here)
 */
static img2_status_t webp_add_metadata(const uint8_t *webp, size_t webp_size, const img2_encode_meta_t *meta, WebPData *assembled)
{
	WebPData image = { webp, webp_size };
	WebPMux *mux = WebPMuxCreate(&image, 0); /* no copy: the bitstream outlives the mux */
	if (mux == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to create the WebP muxer");
		return IMG2_E_NOMEM; /* the input was just produced by WebPEncode: only an allocation can fail */
	}
	WebPMuxError err = WEBP_MUX_OK;
	if (meta->exif != NULL) {
		WebPData chunk = { meta->exif, meta->exif_len };
		err = WebPMuxSetChunk(mux, "EXIF", &chunk, 0);
	}
	if (err == WEBP_MUX_OK && meta->xmp != NULL) {
		WebPData chunk = { (const uint8_t *)meta->xmp, meta->xmp_len };
		err = WebPMuxSetChunk(mux, "XMP ", &chunk, 0);
	}
	if (err == WEBP_MUX_OK) {
		err = WebPMuxAssemble(mux, assembled); /* reads the chunks above: before WebPMuxDelete */
	}
	WebPMuxDelete(mux);
	if (err != WEBP_MUX_OK) {
		/* the bitstream was just produced by WebPEncode and the fourccs / payloads are valid: the only error left
		 * is WEBP_MUX_MEMORY_ERROR */
		img2_log(IMG2_LOG_ERROR, "Error: Failed to add the WebP metadata chunks (WebPMux error %d)", (int)err);
		WebPDataClear(assembled);
		return IMG2_E_NOMEM;
	}
	return IMG2_OK;
}

/**
 * @brief img2_encode_webp() with encode options (NULL = defaults)
 *
 * WebPConfig + WebPEncode, set up like libwebp's simple API (WebPEncodeRGBA / WebPEncodeLosslessRGBA:
 * WEBP_PRESET_DEFAULT, method 4, alpha_quality 100, lossless with quality factor 70), so lossy default output
 * is the simple API's. lossless (or quality 100): lossless mode with `exact` (the RGB of fully transparent
 * pixels is kept, unlike WebPEncodeLosslessRGBA); effort e: method round((e-1)*6/9);
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
	config.exact = lossless ? 1 : 0; /* keep the RGB under alpha 0 (docs/encoder-api.md decision 6) */
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

	/* Metadata (opts->metadata): EXIF / XMP chunks added by WebPMux (VP8X container); none requested or present:
	 * the bitstream above as is */
	img2_encode_meta_t meta;
	if (img2_encode_meta_prepare(img, o, &meta) != IMG2_OK) {
		WebPFree(webp_buffer);
		return IMG2_E_NOMEM;
	}
	WebPData assembled;
	WebPDataInit(&assembled);
	if (meta.exif != NULL || meta.xmp != NULL) {
		img2_status_t mst = webp_add_metadata(webp_buffer, webp_size, &meta, &assembled);
		img2_encode_meta_release(&meta);
		WebPFree(webp_buffer);
		if (mst != IMG2_OK) {
			return mst;
		}
		webp_buffer = (uint8_t *)assembled.bytes;
		webp_size = assembled.size;
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

	/* Free WebP internal buffer (or WebPMux's assembled copy, also WebPMalloc'ed) */
	WebPFree(webp_buffer);

	return IMG2_OK;
}
