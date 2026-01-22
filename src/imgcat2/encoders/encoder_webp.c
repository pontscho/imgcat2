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

#include "../encoders/encoder.h"

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
int encode_webp(const image_t *img, int quality, uint8_t **out_data, size_t *out_size)
{
	if (img == NULL || out_data == NULL || out_size == NULL) {
		fprintf(stderr, "Error: Invalid parameters to encode_webp\n");
		return -1;
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

	/* Encode to WebP */
	uint8_t *webp_buffer = NULL;
	size_t webp_size = 0;

	if (webp_quality == 100) {
		/* Lossless mode */
		webp_size = WebPEncodeLosslessRGBA(img->pixels, img->width, img->height, img->width * 4, &webp_buffer);
	} else {
		/* Lossy mode */
		webp_size = WebPEncodeRGBA(img->pixels, img->width, img->height, img->width * 4, (float)webp_quality, &webp_buffer);
	}

	if (webp_size == 0 || webp_buffer == NULL) {
		fprintf(stderr, "Error: Failed to encode WebP image\n");
		return -1;
	}

	/* Allocate and copy output data */
	*out_data = (uint8_t *)malloc(webp_size);
	if (*out_data == NULL) {
		fprintf(stderr, "Error: Failed to allocate output buffer (%zu bytes)\n", webp_size);
		WebPFree(webp_buffer);
		return -1;
	}

	memcpy(*out_data, webp_buffer, webp_size);
	*out_size = webp_size;

	/* Free WebP internal buffer */
	WebPFree(webp_buffer);

	return 0;
}
