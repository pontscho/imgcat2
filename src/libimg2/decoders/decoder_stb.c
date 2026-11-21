/**
 * @file decoder_stb.c
 * @brief STB image decoder implementation (fallback for multiple formats)
 *
 * Uses stb_image.h to decode PNG, JPEG, BMP, TGA, PSD, HDR, PNM formats.
 * This is the fallback decoder when native libraries are not available.
 */

#include <stdio.h>
#include <stdlib.h>

#include "decoder.h"

/* Define STB_IMAGE_IMPLEMENTATION once in this file */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO        /* We provide data buffer directly */
#define STBI_FAILURE_USERMSG /* Use custom error messages */
#include "stb_image.h"

/**
 * @brief Decode image using stb_image (fallback decoder)
 *
 * Decodes images in formats: PNG, JPEG, BMP, TGA, PSD, HDR, PNM.
 * This is a generic decoder that works for all STB-supported formats.
 *
 * @param data Raw image file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (static image)
 * @return Array with single image_t*, or NULL on error
 *
 * @note STB only supports static images (no animation)
 * @note Returns RGBA8888 format (4 channels, 8 bits per channel)
 */
image_t **decode_stb(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_stb\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Decode with stb_image
	int width, height, channels;
	uint8_t *pixels = stbi_load_from_memory(data, (int)len, &width, &height, &channels, 4);
	if (pixels == NULL) {
		fprintf(stderr, "Error: stb_image failed to decode: %s\n", stbi_failure_reason());
		return NULL;
	}

	// Validate dimensions
	if (width <= 0 || height <= 0) {
		fprintf(stderr, "Error: stb_image returned invalid dimensions: %dx%d\n", width, height);
		stbi_image_free(pixels);
		return NULL;
	}

	// Create image_t structure
	image_t *img = image_create((uint32_t)width, (uint32_t)height);
	if (img == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		stbi_image_free(pixels);
		return NULL;
	}

	// Copy pixel data (stb_image returns RGBA8888 when we request 4 channels)
	size_t pixel_count = (size_t)width * (size_t)height * 4;
	memcpy(img->pixels, pixels, pixel_count);

	// Free stb_image buffer
	stbi_image_free(pixels);

	// Allocate frames array (single frame)
	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(img);
		return NULL;
	}

	frames[0] = img;
	*frame_count = 1;

	return frames;
}

/**
 * @brief Decode BMP image using stb_image
 *
 * Wrapper around decode_stb() for BMP format.
 * Handles various BMP bit depths (1, 4, 8, 16, 24, 32).
 *
 * @param data Raw BMP file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (BMP is static)
 * @return Array with single image_t*, or NULL on error
 */
image_t **decode_bmp(const uint8_t *data, size_t len, int *frame_count)
{
	return decode_stb(data, len, frame_count);
}

/**
 * @brief Decode TGA (Targa) image using stb_image
 *
 * Wrapper around decode_stb() for TGA format.
 * Handles uncompressed and RLE-compressed TGA images.
 *
 * @param data Raw TGA file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (TGA is static)
 * @return Array with single image_t*, or NULL on error
 */
image_t **decode_tga(const uint8_t *data, size_t len, int *frame_count)
{
	return decode_stb(data, len, frame_count);
}

/**
 * @brief Decode PSD (Photoshop) image using stb_image
 *
 * Wrapper around decode_stb() for PSD format.
 * Only decodes composite view (no layer extraction).
 *
 * @param data Raw PSD file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (PSD is static)
 * @return Array with single image_t*, or NULL on error
 *
 * @note stb_image only decodes the composited view, not individual layers
 */
image_t **decode_psd(const uint8_t *data, size_t len, int *frame_count)
{
	return decode_stb(data, len, frame_count);
}

/**
 * @brief Decode HDR (Radiance RGBE) image using stb_image
 *
 * Decodes HDR images with tone mapping to convert float RGB to RGBA8888.
 * Uses simple clamping tone mapping.
 *
 * @param data Raw HDR file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (HDR is static)
 * @return Array with single image_t*, or NULL on error
 *
 * @note HDR images store float RGB values
 * @note Tone mapping converts [0.0, inf] → [0, 255] with clamping
 */
image_t **decode_hdr(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_hdr\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Decode HDR with stb_image (float output)
	int width, height, channels;
	float *pixels_float = stbi_loadf_from_memory(data, (int)len, &width, &height, &channels, 3);
	if (pixels_float == NULL) {
		fprintf(stderr, "Error: stb_image failed to decode HDR: %s\n", stbi_failure_reason());
		return NULL;
	}

	// Validate dimensions
	if (width <= 0 || height <= 0) {
		fprintf(stderr, "Error: stb_image returned invalid dimensions: %dx%d\n", width, height);
		stbi_image_free(pixels_float);
		return NULL;
	}

	// Create image_t structure
	image_t *img = image_create((uint32_t)width, (uint32_t)height);
	if (img == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		stbi_image_free(pixels_float);
		return NULL;
	}

	// Tone mapping: convert float RGB [0.0, inf] to RGBA8888 [0, 255]
	// Simple clamping: clamp [0.0, 1.0] then scale to [0, 255]
	size_t pixel_count = (size_t)width * (size_t)height;
	for (size_t i = 0; i < pixel_count; i++) {
		float r = pixels_float[i * 3 + 0];
		float g = pixels_float[i * 3 + 1];
		float b = pixels_float[i * 3 + 2];

		// Clamp to [0.0, 1.0]
		if (r < 0.0f) {
			r = 0.0f;
		}
		if (r > 1.0f) {
			r = 1.0f;
		}
		if (g < 0.0f) {
			g = 0.0f;
		}
		if (g > 1.0f) {
			g = 1.0f;
		}
		if (b < 0.0f) {
			b = 0.0f;
		}
		if (b > 1.0f) {
			b = 1.0f;
		}

		// Convert to [0, 255]
		uint8_t r8 = (uint8_t)(r * 255.0f);
		uint8_t g8 = (uint8_t)(g * 255.0f);
		uint8_t b8 = (uint8_t)(b * 255.0f);

		// Set pixel (alpha = 255)
		img->pixels[i * 4 + 0] = r8;
		img->pixels[i * 4 + 1] = g8;
		img->pixels[i * 4 + 2] = b8;
		img->pixels[i * 4 + 3] = 255;
	}

	// Free stb_image float buffer
	stbi_image_free(pixels_float);

	// Allocate frames array (single frame)
	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(img);
		return NULL;
	}

	frames[0] = img;
	*frame_count = 1;

	// fprintf(stderr, "HDR decoded: %ux%u, tone-mapped to RGBA8888\n", (uint32_t)width, (uint32_t)height);

	return frames;
}

/**
 * @brief Decode PNM (PPM/PGM) image using stb_image
 *
 * Wrapper around decode_stb() for PNM format.
 * Handles PPM (color) and PGM (grayscale) in binary format (P5, P6).
 *
 * @param data Raw PNM file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (PNM is static)
 * @return Array with single image_t*, or NULL on error
 *
 * @note Only supports binary PNM formats (P5, P6), not ASCII formats
 */
image_t **decode_pnm(const uint8_t *data, size_t len, int *frame_count)
{
	return decode_stb(data, len, frame_count);
}
