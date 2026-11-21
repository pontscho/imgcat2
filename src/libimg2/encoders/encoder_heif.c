/**
 * @file encoder_heif.c
 * @brief HEIF encoder implementation using libheif
 *
 * Encodes RGBA8888 images to HEIF format using libheif with HEVC compression.
 * Preserves alpha channel (HEIF supports transparency).
 */

/* clang-format off */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libheif/heif.h>
/* clang-format on */

#include "../encoders/encoder.h"

/**
 * @brief HEIF memory write state structure
 *
 * Used by heif_memory_write_func() to accumulate HEIF output data in memory.
 */
typedef struct {
	uint8_t *buffer; /**< Output buffer (dynamically resized) */
	size_t size; /**< Current size of valid data in buffer */
	size_t capacity; /**< Total allocated capacity of buffer */
} heif_memory_write_state;

/**
 * @brief HEIF memory write callback
 *
 * Custom write function for libheif that writes HEIF data to a memory buffer
 * instead of a file. Automatically resizes the buffer as needed.
 *
 * @param ctx HEIF context (unused)
 * @param data Data to write
 * @param size Number of bytes to write
 * @param userdata Pointer to heif_memory_write_state
 * @return heif_error structure (heif_error_Ok on success)
 */
static struct heif_error heif_memory_write_func(struct heif_context *ctx, const void *data, size_t size, void *userdata)
{
	(void)ctx; /* Unused */

	heif_memory_write_state *state = (heif_memory_write_state *)userdata;

	if (state == NULL) {
		struct heif_error err = { heif_error_Usage_error, heif_suberror_Null_pointer_argument, "Write error: NULL state pointer" };
		return err;
	}

	/* Check if we need to resize buffer */
	if (state->size + size > state->capacity) {
		/* Double capacity until it fits */
		size_t new_capacity = state->capacity;
		if (new_capacity == 0) {
			new_capacity = 65536; /* Initial capacity: 64KB */
		}
		while (new_capacity < state->size + size) {
			new_capacity *= 2;
		}

		/* Reallocate buffer */
		uint8_t *new_buffer = (uint8_t *)realloc(state->buffer, new_capacity);
		if (new_buffer == NULL) {
			struct heif_error err = { heif_error_Memory_allocation_error, heif_suberror_Unspecified, "Write error: Failed to reallocate buffer" };
			return err;
		}

		state->buffer = new_buffer;
		state->capacity = new_capacity;
	}

	/* Append data to buffer */
	memcpy(state->buffer + state->size, data, size);
	state->size += size;

	struct heif_error success = { heif_error_Ok, heif_suberror_Unspecified, "Success" };
	return success;
}

/**
 * @brief Encode RGBA image to HEIF format
 *
 * Encodes RGBA8888 image to HEIF format with quality control using HEVC compression.
 * Preserves alpha channel (HEIF supports transparency).
 *
 * @param img Input image in RGBA8888 format
 * @param quality HEIF quality level (0-100, higher = better quality, larger file)
 * @param out_data Output parameter for encoded data (caller must free)
 * @param out_size Output parameter for encoded data size in bytes
 * @return 0 on success, -1 on error
 *
 * @note HEIF preserves alpha channel (supports transparency)
 * @note Caller must free *out_data with free() when done
 * @note Requires HEVC encoder to be available in libheif
 */
int encode_heif(const image_t *img, int quality, uint8_t **out_data, size_t *out_size)
{
	if (img == NULL || out_data == NULL || out_size == NULL) {
		fprintf(stderr, "Error: Invalid parameters to encode_heif\n");
		return -1;
	}

	/* Initialize outputs */
	*out_data = NULL;
	*out_size = 0;

	/* Clamp quality to valid range [0, 100] */
	int heif_quality = quality;
	if (heif_quality < 0) {
		heif_quality = 0;
	}
	if (heif_quality > 100) {
		heif_quality = 100;
	}

	/* Create HEIF context */
	struct heif_context *ctx = heif_context_alloc();
	if (ctx == NULL) {
		fprintf(stderr, "Error: Failed to allocate HEIF context\n");
		return -1;
	}

	/* Create HEIF image from RGBA8888 */
	struct heif_image *heif_img = NULL;
	struct heif_error err = heif_image_create(img->width, img->height, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, &heif_img);

	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to create HEIF image: %s\n", err.message);
		heif_context_free(ctx);
		return -1;
	}

	/* Add image plane */
	err = heif_image_add_plane(heif_img, heif_channel_interleaved, img->width, img->height, 8);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to add HEIF image plane: %s\n", err.message);
		heif_image_release(heif_img);
		heif_context_free(ctx);
		return -1;
	}

	/* Get plane pointer and stride */
	int stride = 0;
	uint8_t *plane = heif_image_get_plane(heif_img, heif_channel_interleaved, &stride);
	if (plane == NULL) {
		fprintf(stderr, "Error: Failed to get HEIF image plane\n");
		heif_image_release(heif_img);
		heif_context_free(ctx);
		return -1;
	}

	/* Copy pixel data row-by-row (handle stride) */
	for (uint32_t y = 0; y < img->height; y++) {
		const uint8_t *src_row = img->pixels + y * img->width * 4;
		uint8_t *dst_row = plane + y * stride;
		memcpy(dst_row, src_row, img->width * 4);
	}

	/* Get HEVC encoder */
	struct heif_encoder *encoder = NULL;
	err = heif_context_get_encoder_for_format(ctx, heif_compression_HEVC, &encoder);
	if (err.code != heif_error_Ok || encoder == NULL) {
		fprintf(stderr, "Error: No HEVC encoder available: %s\n", err.message);
		heif_image_release(heif_img);
		heif_context_free(ctx);
		return -1;
	}

	/* Set quality */
	err = heif_encoder_set_lossy_quality(encoder, heif_quality);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to set HEIF quality: %s\n", err.message);
		heif_encoder_release(encoder);
		heif_image_release(heif_img);
		heif_context_free(ctx);
		return -1;
	}

	/* Encode image */
	struct heif_image_handle *handle = NULL;
	err = heif_context_encode_image(ctx, heif_img, encoder, NULL, &handle);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to encode HEIF image: %s\n", err.message);
		heif_encoder_release(encoder);
		heif_image_release(heif_img);
		heif_context_free(ctx);
		return -1;
	}

	/* Release encoder (no longer needed) */
	heif_encoder_release(encoder);

	/* Initialize memory write state */
	heif_memory_write_state write_state;
	write_state.buffer = NULL;
	write_state.size = 0;
	write_state.capacity = 0;

	/* Setup heif_writer with our memory write callback */
	struct heif_writer writer;
	writer.writer_api_version = 1;
	writer.write = heif_memory_write_func;

	/* Write to memory using our callback */
	err = heif_context_write(ctx, &writer, &write_state);
	if (err.code != heif_error_Ok) {
		fprintf(stderr, "Error: Failed to write HEIF to memory: %s\n", err.message);
		heif_image_handle_release(handle);
		heif_image_release(heif_img);
		heif_context_free(ctx);
		if (write_state.buffer != NULL) {
			free(write_state.buffer);
		}
		return -1;
	}

	/* Cleanup HEIF structures */
	heif_image_handle_release(handle);
	heif_image_release(heif_img);
	heif_context_free(ctx);

	/* Check output */
	if (write_state.buffer == NULL || write_state.size == 0) {
		fprintf(stderr, "Error: HEIF encoder produced no output\n");
		if (write_state.buffer != NULL) {
			free(write_state.buffer);
		}
		return -1;
	}

	/* Allocate and copy output data */
	*out_data = (uint8_t *)malloc(write_state.size);
	if (*out_data == NULL) {
		fprintf(stderr, "Error: Failed to allocate output buffer (%zu bytes)\n", write_state.size);
		free(write_state.buffer);
		return -1;
	}

	memcpy(*out_data, write_state.buffer, write_state.size);
	*out_size = write_state.size;

	/* Free write state buffer */
	free(write_state.buffer);

	return 0;
}
