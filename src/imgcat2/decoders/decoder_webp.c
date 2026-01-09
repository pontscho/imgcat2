/**
 * @file decoder_webp.c
 * @brief WebP decoder implementation using libwebp
 *
 * Decodes WebP images (both static and animated) to RGBA8888 format.
 * Handles transparency and animation frame composition.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webp/decode.h>
#include <webp/demux.h>
/* clang-format on */

#include "decoder.h"

/** Maximum number of WebP frames to decode (prevents DoS) */
#define MAX_WEBP_FRAMES 200

/**
 * @brief Decode static WebP image (single frame)
 *
 * Decodes a static WebP image to RGBA8888 format.
 * For animated WebP images, use decode_webp_animated().
 *
 * @param data Raw WebP file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 */
static image_t **decode_webp_static(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_webp_static\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Decode WebP to RGBA
	int width = 0;
	int height = 0;
	uint8_t *pixels = WebPDecodeRGBA(data, len, &width, &height);
	if (pixels == NULL) {
		fprintf(stderr, "Error: Failed to decode WebP image\n");
		return NULL;
	}

	// Create image_t structure
	image_t *img = image_create((uint32_t)width, (uint32_t)height);
	if (img == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		WebPFree(pixels);
		return NULL;
	}

	// Copy pixels to image_t (WebP returns RGBA8888, same as our format)
	size_t pixel_size = (size_t)width * (size_t)height * 4;
	memcpy(img->pixels, pixels, pixel_size);

	// Free WebP decoder buffer
	WebPFree(pixels);

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
 * @brief Check if WebP is animated (has multiple frames)
 *
 * Quickly checks if a WebP file contains multiple frames without
 * fully decoding the image data. Useful for determining whether
 * to use static or animated decoder.
 *
 * @param data Raw WebP file data
 * @param len Size of data in bytes
 * @return true if WebP has animation, false otherwise or on error
 *
 * @note Returns false if data is invalid or not a WebP
 * @note Does not validate that frames are decodable, only checks for animation flag
 */
static bool webp_is_animated(const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0) {
		return false;
	}

	// Get WebP features
	WebPBitstreamFeatures features;
	if (WebPGetFeatures(data, len, &features) != VP8_STATUS_OK) {
		return false;
	}

	// Check if animated
	return features.has_animation != 0;
}

/**
 * @brief Decode animated WebP with all frames
 *
 * Decodes all frames of an animated WebP with proper frame composition.
 * WebP's animation decoder automatically handles frame composition.
 *
 * @param data Raw WebP file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Maximum MAX_WEBP_FRAMES frames (200) to prevent DoS
 * @note WebP decoder returns fully composited frames automatically
 * @note Output format is RGBA8888
 */
static image_t **decode_webp_animated(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_webp_animated\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Setup WebPData
	WebPData webp_data;
	webp_data.bytes = data;
	webp_data.size = len;

	// Initialize decoder options
	WebPAnimDecoderOptions dec_options;
	if (!WebPAnimDecoderOptionsInit(&dec_options)) {
		fprintf(stderr, "Error: Failed to initialize WebP decoder options\n");
		return NULL;
	}

	dec_options.color_mode = MODE_RGBA;
	dec_options.use_threads = 1;

	// Create decoder
	WebPAnimDecoder *dec = WebPAnimDecoderNew(&webp_data, &dec_options);
	if (dec == NULL) {
		fprintf(stderr, "Error: Failed to create WebP animation decoder\n");
		return NULL;
	}

	// Get animation info
	WebPAnimInfo anim_info;
	if (!WebPAnimDecoderGetInfo(dec, &anim_info)) {
		fprintf(stderr, "Error: Failed to get WebP animation info\n");
		WebPAnimDecoderDelete(dec);
		return NULL;
	}

	// Check frame count
	int num_frames = (int)anim_info.frame_count;
	if (num_frames == 0) {
		fprintf(stderr, "Error: WebP animation has no frames\n");
		WebPAnimDecoderDelete(dec);
		return NULL;
	}

	if (num_frames > MAX_WEBP_FRAMES) {
		fprintf(stderr, "Warning: WebP has %d frames, limiting to %d\n", num_frames, MAX_WEBP_FRAMES);
		num_frames = MAX_WEBP_FRAMES;
	}

	// Get canvas dimensions
	uint32_t canvas_width = anim_info.canvas_width;
	uint32_t canvas_height = anim_info.canvas_height;

	// Allocate frames array
	image_t **frames = (image_t **)malloc(sizeof(image_t *) * num_frames);
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		WebPAnimDecoderDelete(dec);
		return NULL;
	}

	// Initialize frames to NULL for cleanup
	for (int i = 0; i < num_frames; i++) {
		frames[i] = NULL;
	}

	// Decode each frame
	int frame_idx = 0;
	while (WebPAnimDecoderHasMoreFrames(dec) && frame_idx < num_frames) {
		uint8_t *frame_buf;
		int timestamp;

		if (!WebPAnimDecoderGetNext(dec, &frame_buf, &timestamp)) {
			fprintf(stderr, "Error: Failed to decode WebP frame %d\n", frame_idx);
			goto cleanup_error;
		}

		// Create output frame
		frames[frame_idx] = image_create(canvas_width, canvas_height);
		if (frames[frame_idx] == NULL) {
			fprintf(stderr, "Error: Failed to create output frame %d\n", frame_idx);
			goto cleanup_error;
		}

		// Copy frame buffer to image_t
		// WebP decoder returns fully composited RGBA frames
		size_t pixel_size = (size_t)canvas_width * (size_t)canvas_height * 4;
		memcpy(frames[frame_idx]->pixels, frame_buf, pixel_size);

		frame_idx++;
	}

	// Cleanup
	WebPAnimDecoderDelete(dec);

	*frame_count = frame_idx;

	return frames;

cleanup_error:
	// Cleanup on error
	for (int i = 0; i < num_frames; i++) {
		if (frames[i] != NULL) {
			image_destroy(frames[i]);
		}
	}
	free(frames);
	WebPAnimDecoderDelete(dec);
	return NULL;
}

/**
 * @brief Decode WebP image (static or animated)
 *
 * Main entry point for WebP decoding. Automatically detects if the image
 * is animated and routes to the appropriate decoder function.
 *
 * @param data Raw WebP file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (1 for static, N for animated)
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with decoder_free_frames()
 * @note For static images, frame_count = 1
 * @note For animated images, frame_count = N (max MAX_WEBP_FRAMES)
 * @note Output format is RGBA8888
 */
image_t **decode_webp(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_webp\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Check if animated
	if (webp_is_animated(data, len)) {
		return decode_webp_animated(data, len, frame_count);
	} else {
		return decode_webp_static(data, len, frame_count);
	}
}
