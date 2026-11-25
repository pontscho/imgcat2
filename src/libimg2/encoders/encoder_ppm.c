/**
 * @file encoder_ppm.c
 * @brief Native Netpbm encoder: binary PPM (P6) or PGM (P5), maxval 255
 *
 * Encodes RGBA8888 images to uncompressed Netpbm without any codec library. Netpbm P5/P6 has no alpha channel:
 * every pixel is composited onto opts->background (0xRRGGBB, default black). When every composited pixel is
 * gray (R == G == B) the output is P5 (one sample per pixel), otherwise P6 (three samples per pixel).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/log.h"
#include "encoder.h"

/**
 * @brief Composite one colour channel onto the background channel
 *
 * out = (c * a + bg * (255 - a) + 127) / 255, rounded; a = 255 gives c exactly, a = 0 gives bg exactly.
 *
 * @param c Channel value of the pixel
 * @param a Alpha of the pixel
 * @param bg Channel value of the background
 * @return Composited channel value (0..255)
 */
static inline uint8_t ppm_composite(uint8_t c, uint8_t a, uint8_t bg)
{
	return (uint8_t)(((unsigned)c * a + (unsigned)bg * (255u - a) + 127u) / 255u);
}

/**
 * @brief Log one WARN per option PPM cannot honour (lossless is a silent no-op: PPM is always lossless)
 *
 * @param o Encode options (not NULL)
 */
static void ppm_warn_options(const img2_encode_opts_t *o)
{
	if (o->quality >= 0) {
		img2_encode_warn_ignored("quality", "PPM", "uncompressed");
	}
	if (o->effort > 0) {
		img2_encode_warn_ignored("effort", "PPM", "uncompressed");
	}
	if (o->chroma != IMG2_CHROMA_DEFAULT) {
		char option[16];
		snprintf(option, sizeof(option), "chroma %s", img2_chroma_name(o->chroma));
		img2_encode_warn_ignored(option, "PPM", "always RGB or gray");
	}
	if (o->alpha_quality > 0) {
		img2_encode_warn_ignored("alpha_quality", "PPM", "no alpha channel");
	}
	if (o->progressive) {
		img2_encode_warn_ignored("progressive", "PPM", "no progressive mode");
	}
	if (o->threads > 0) {
		img2_encode_warn_ignored("threads", "PPM", "single-threaded encoder");
	}
	if (o->metadata != 0) {
		img2_encode_warn_ignored("metadata", "PPM", "no metadata container");
	}
}

/**
 * @brief Encode RGBA image to binary PPM / PGM with default options (alpha composited onto black)
 *
 * @param img Input image in RGBA8888 format
 * @param quality Ignored (PPM is uncompressed)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return IMG2_OK; IMG2_E_ARG for NULL arguments; IMG2_E_NOMEM
 *
 * @note Caller must free *out_data with free() when done
 */
img2_status_t img2_encode_ppm(const img2_image_t *img, int quality, uint8_t **out_data, size_t *out_size)
{
	return img2_encode_ppm_opts(img, quality, NULL, out_data, out_size);
}

/**
 * @brief img2_encode_ppm() with encode options (NULL = defaults)
 *
 * background: the 0xRRGGBB colour alpha is composited onto (0 = black). lossless: no-op (always lossless).
 * quality, effort, chroma, alpha_quality, progressive, threads and metadata have no PPM equivalent (one WARN
 * each).
 *
 * @param img Input image in RGBA8888 format (packed rows)
 * @param native_quality Ignored (PPM is uncompressed; opts->quality >= 0 logs a WARN)
 * @param opts Encode options, or NULL for defaults
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return IMG2_OK; IMG2_E_ARG for NULL arguments; IMG2_E_NOMEM
 */
img2_status_t img2_encode_ppm_opts(const img2_image_t *img, int native_quality, const img2_encode_opts_t *opts, uint8_t **out_data, size_t *out_size)
{
	(void)native_quality;
	if (img == NULL || out_data == NULL || out_size == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Invalid parameters to encode_ppm");
		return IMG2_E_ARG;
	}

	/* Initialize outputs */
	*out_data = NULL;
	*out_size = 0;

	const img2_encode_opts_t *o = img2_encode_opts_or_default(opts);
	ppm_warn_options(o);

	const uint8_t bg_r = (uint8_t)((o->background >> 16) & 0xFFu);
	const uint8_t bg_g = (uint8_t)((o->background >> 8) & 0xFFu);
	const uint8_t bg_b = (uint8_t)(o->background & 0xFFu);
	const uint8_t *px = img->pixels;
	const size_t count = (size_t)img->width * img->height; /* <= IMG2_MAX_PIXELS: count * 3 fits size_t */

	/* Pass 1: P5 if every composited pixel is gray (stops at the first colour pixel) */
	size_t channels = 1;
	for (size_t i = 0; i < count; i++) {
		const uint8_t *p = px + i * 4;
		const uint8_t r = ppm_composite(p[0], p[3], bg_r);
		const uint8_t g = ppm_composite(p[1], p[3], bg_g);
		const uint8_t b = ppm_composite(p[2], p[3], bg_b);
		if (r != g || g != b) {
			channels = 3;
			break;
		}
	}

	/* Header: width and height are <= IMG2_MAX_DIMENSION (65536, 5 digits), so "P6\n65536 65536\n255\n" is at most
	 * 19 bytes and always fits hdr; the returned length is the write offset and part of the allocation size */
	char hdr[32];
	const size_t hdr_len = (size_t)snprintf(hdr, sizeof(hdr), "P%c\n%u %u\n255\n", channels == 1 ? '5' : '6', (unsigned)img->width, (unsigned)img->height);

	const size_t total = hdr_len + count * channels;
	uint8_t *buf = (uint8_t *)malloc(total);
	if (buf == NULL) {
		img2_log(IMG2_LOG_ERROR, "Error: Failed to allocate PPM output buffer (%zu bytes)", total);
		return IMG2_E_NOMEM;
	}
	memcpy(buf, hdr, hdr_len);

	/* Pass 2: composite and write channels samples per pixel */
	uint8_t *dst = buf + hdr_len;
	for (size_t i = 0; i < count; i++) {
		const uint8_t *p = px + i * 4;
		dst[0] = ppm_composite(p[0], p[3], bg_r);
		if (channels == 3) {
			dst[1] = ppm_composite(p[1], p[3], bg_g);
			dst[2] = ppm_composite(p[2], p[3], bg_b);
		}
		dst += channels;
	}

	*out_data = buf;
	*out_size = total;
	return IMG2_OK;
}
