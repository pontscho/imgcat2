/**
 * @file decoder_heif.c
 * @brief HEIF decoder implementation using libheif
 *
 * Decodes HEIF/HEIC images (both static and image sequences) to RGBA8888 format.
 * Handles both single images and burst photo sequences.
 */

/* clang-format off */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libheif/heif.h>
/* clang-format on */

#include "decoder.h"

/** Maximum number of HEIF frames to decode (prevents DoS) */
#define MAX_HEIF_FRAMES 200

/**
 * @brief Check if HEIF is an image sequence (has multiple images)
 *
 * Quickly checks if a HEIF file contains multiple top-level images without
 * fully decoding the image data. Useful for determining whether
 * to use static or animated decoder.
 *
 * @param data Raw HEIF file data
 * @param len Size of data in bytes
 * @return true if HEIF has multiple images, false otherwise or on error
 *
 * @note Returns false if data is invalid or not a HEIF
 * @note Does not validate that images are decodable, only checks count
 */
static bool heif_is_animated(const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0) {
		return false;
	}

	// Create HEIF context
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		return false;
	}

	// Read from memory
	struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
	if (err.code != heif_error_Ok) {
		heif_context_free(ctx);
		return false;
	}

	// Get number of top-level images
	int num_images = heif_context_get_number_of_top_level_images(ctx);

	// Cleanup
	heif_context_free(ctx);

	// Image sequence if more than 1 image
	return num_images > 1;
}

/**
 * @brief Decode static HEIF image (single frame)
 *
 * Decodes a static HEIF image to RGBA8888 format.
 * For HEIF image sequences, use decode_heif_animated().
 *
 * @param data Raw HEIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: always 1 (single frame)
 * @return Array with single image_t*, or NULL on error
 *
 * @note Output format is RGBA8888
 */
static image_t **decode_heif_static(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_heif_static\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Create HEIF context
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		fprintf(stderr, "Error: Failed to allocate HEIF context\n");
		return NULL;
	}

	// Read from memory
	struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to read HEIF data: %s\n", err.message);
		heif_context_free(ctx);
		return NULL;
	}

	// Get primary image handle
	struct heif_image_handle *handle = NULL;
	err = heif_context_get_primary_image_handle(ctx, &handle);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to get primary image handle: %s\n", err.message);
		heif_context_free(ctx);
		return NULL;
	}

	// Decode image to RGBA
	struct heif_image *img = NULL;
	err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to decode HEIF image: %s\n", err.message);
		heif_image_handle_release(handle);
		heif_context_free(ctx);
		return NULL;
	}

	// Get image dimensions
	int width = heif_image_get_width(img, heif_channel_interleaved);
	int height = heif_image_get_height(img, heif_channel_interleaved);

	// Get RGBA plane
	int stride = 0;
	const uint8_t *plane = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
	if (plane == NULL) {
		fprintf(stderr, "Error: Failed to get HEIF image plane\n");
		heif_image_release(img);
		heif_image_handle_release(handle);
		heif_context_free(ctx);
		return NULL;
	}

	// Create image_t structure
	image_t *output = image_create((uint32_t)width, (uint32_t)height);
	if (output == NULL) {
		fprintf(stderr, "Error: Failed to create image_t structure\n");
		heif_image_release(img);
		heif_image_handle_release(handle);
		heif_context_free(ctx);
		return NULL;
	}

	// Copy pixels row-by-row (handle stride != width*4)
	for (int y = 0; y < height; y++) {
		const uint8_t *src_row = plane + y * stride;
		uint8_t *dst_row = output->pixels + y * width * 4;
		memcpy(dst_row, src_row, width * 4);
	}

	// Cleanup HEIF resources
	heif_image_release(img);
	heif_image_handle_release(handle);
	heif_context_free(ctx);

	// Allocate frames array (single frame)
	image_t **frames = (image_t **)malloc(sizeof(image_t *));
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		image_destroy(output);
		return NULL;
	}

	frames[0] = output;
	*frame_count = 1;

	return frames;
}

/**
 * @brief Decode HEIF image sequence with all frames
 *
 * Decodes all images in a HEIF image sequence (e.g., burst photos).
 * Each image is decoded independently.
 *
 * @param data Raw HEIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Maximum MAX_HEIF_FRAMES frames (200) to prevent DoS
 * @note Unlike GIF/WebP, HEIF sequences have no timing info - uniform frame rate
 * @note Output format is RGBA8888
 */
static image_t **decode_heif_animated(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_heif_animated\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Create HEIF context
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		fprintf(stderr, "Error: Failed to allocate HEIF context\n");
		return NULL;
	}

	// Read from memory
	struct heif_error err = heif_context_read_from_memory_without_copy(ctx, data, len, NULL);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to read HEIF data: %s\n", err.message);
		heif_context_free(ctx);
		return NULL;
	}

	// Get number of top-level images
	int num_images = heif_context_get_number_of_top_level_images(ctx);
	if (num_images == 0) {
		fprintf(stderr, "Error: HEIF image sequence has no images\n");
		heif_context_free(ctx);
		return NULL;
	}

	// Enforce MAX_HEIF_FRAMES limit
	if (num_images > MAX_HEIF_FRAMES) {
		fprintf(stderr, "Warning: HEIF has %d images, limiting to %d\n", num_images, MAX_HEIF_FRAMES);
		num_images = MAX_HEIF_FRAMES;
	}

	// Get list of image IDs
	heif_item_id *image_ids = (heif_item_id *)malloc(sizeof(heif_item_id) * num_images);
	if (image_ids == NULL) {
		fprintf(stderr, "Error: Failed to allocate image ID array\n");
		heif_context_free(ctx);
		return NULL;
	}

	int actual_count = heif_context_get_list_of_top_level_image_IDs(ctx, image_ids, num_images);
	if (actual_count != num_images) {
		fprintf(stderr, "Error: Failed to get image ID list\n");
		free(image_ids);
		heif_context_free(ctx);
		return NULL;
	}

	// Allocate frames array
	image_t **frames = (image_t **)malloc(sizeof(image_t *) * num_images);
	if (frames == NULL) {
		fprintf(stderr, "Error: Failed to allocate frames array\n");
		free(image_ids);
		heif_context_free(ctx);
		return NULL;
	}

	// Initialize frames to NULL for cleanup
	for (int i = 0; i < num_images; i++) {
		frames[i] = NULL;
	}

	// Decode each image
	for (int i = 0; i < num_images; i++) {
		// Get image handle
		struct heif_image_handle *handle = NULL;
		err = heif_context_get_image_handle(ctx, image_ids[i], &handle);
		if (err.code != heif_error_Ok) {
			fprintf(stderr, "Error: Failed to get image handle %d: %s\n", i, err.message);
			goto cleanup_error;
		}

		// Decode image to RGBA
		struct heif_image *img = NULL;
		err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, NULL);
		if (err.code != heif_error_Ok) {
			fprintf(stderr, "Error: Failed to decode HEIF image %d: %s\n", i, err.message);
			heif_image_handle_release(handle);
			goto cleanup_error;
		}

		// Get dimensions
		int width = heif_image_get_width(img, heif_channel_interleaved);
		int height = heif_image_get_height(img, heif_channel_interleaved);

		// Get RGBA plane
		int stride = 0;
		const uint8_t *plane = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
		if (plane == NULL) {
			fprintf(stderr, "Error: Failed to get HEIF image plane %d\n", i);
			heif_image_release(img);
			heif_image_handle_release(handle);
			goto cleanup_error;
		}

		// Create output frame
		frames[i] = image_create((uint32_t)width, (uint32_t)height);
		if (frames[i] == NULL) {
			fprintf(stderr, "Error: Failed to create output frame %d\n", i);
			heif_image_release(img);
			heif_image_handle_release(handle);
			goto cleanup_error;
		}

		// Copy pixels row-by-row (handle stride != width*4)
		for (int y = 0; y < height; y++) {
			const uint8_t *src_row = plane + y * stride;
			uint8_t *dst_row = frames[i]->pixels + y * width * 4;
			memcpy(dst_row, src_row, width * 4);
		}

		// Release this image's resources
		heif_image_release(img);
		heif_image_handle_release(handle);
	}

	// Cleanup
	free(image_ids);
	heif_context_free(ctx);

	*frame_count = num_images;

	return frames;

cleanup_error:
	// Cleanup on error
	for (int i = 0; i < num_images; i++) {
		if (frames[i] != NULL) {
			image_destroy(frames[i]);
		}
	}
	free(frames);
	free(image_ids);
	heif_context_free(ctx);
	return NULL;
}

/**
 * @brief Decode HEIF image (static or image sequence)
 *
 * Main entry point for HEIF decoding. Automatically detects if the image
 * is a sequence and routes to the appropriate decoder function.
 *
 * @param data Raw HEIF file data
 * @param len Length of data in bytes
 * @param frame_count Output: number of frames decoded (1 for static, N for sequence)
 * @return Array of image_t* frames, or NULL on error
 *
 * @note Caller must free returned array with decoder_free_frames()
 * @note For static images, frame_count = 1
 * @note For image sequences, frame_count = N (max MAX_HEIF_FRAMES)
 * @note Output format is RGBA8888
 */
image_t **decode_heif(const uint8_t *data, size_t len, int *frame_count)
{
	if (data == NULL || len == 0 || frame_count == NULL) {
		fprintf(stderr, "Error: Invalid parameters to decode_heif\n");
		return NULL;
	}

	// Initialize output
	*frame_count = 0;

	// Check if image sequence
	if (heif_is_animated(data, len)) {
		return decode_heif_animated(data, len, frame_count);
	} else {
		return decode_heif_static(data, len, frame_count);
	}
}
